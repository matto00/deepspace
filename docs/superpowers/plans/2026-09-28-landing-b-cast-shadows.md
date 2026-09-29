# Landing Slice (b): Cast Shadows, Baked -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put cast shadows on the ground and on the orbit's worlds, **baked, not marched** (the developer's ruling, 2026-09-28). A pure C++ horizon march over the world's one height function computes the shadow. The ground tiles compute it per vertex while they are built, off the game thread, and carry it in a vertex channel. Each solid world gets one shadow map for the orbit, baked in C++ when the system loads, off the game thread. The two agree at the 50 km handover, use the sky's own sun direction, cost the frame nothing measurable, and are judged by before/after frames.

**Architecture:**

- **The march.** `SunShadow` (`Surface/SunShadow.*`) is pure. It finds the highest horizon toward the star along the great circle through the point, using exact triangle geometry. The visibility is the share of the star's disc (its own angular radius) above that horizon. There are two schedules over one core:
  - `SunShadow::Visible`, geometric samples read through `IGroundField::Height`, for a tile's vertices.
  - `SunShadow::AlongProfile`, every texel ahead in a map column, for the orbit's map.
- **The exits.**
  - The night exit is a proof: no horizon is lower than the dip to the lowest ground.
  - The day exit is a measurement with a margin: the steepest slope over 131,072 samples of the full height, times 1.5.
  - Both in-loop exits are exact.
- **The tiles.** `TerrainTile::Build` takes a `FTileShadow` (the light, the steepest slope, the sample count). It stores `FTileBuild::SunVisible` per grid vertex (skirts copy theirs), and `UTerrainTileComponent` writes it to UV0.x. UV0 was already allocated full-precision and unused, so the channel costs no vertex memory.
- **The orbit's map.** `SunShadowMap` (`Surface/SunShadowMap.*`) is pure, and M_SkyBody reads it.
  - It is an equirectangular map in the star's own frame: Z is the light, columns are the azimuth about it, rows are the star's elevation from `PsiLo` (under which no point of the world sees any of the star) up to the point under the star.
  - A texel is `2 pi / Width` square at the terminator, where the shadows are. The night side past `PsiLo` is not stored, because it is provably dark.
  - Each column's heights are read once. Each texel's horizon is `AlongProfile` over the texels ahead of it.
  - It is quantised to 16 bits and given a mip per level.
  - `AShipSky` bakes one world per task, nearest first, at most `ds.Sky.ShadowBakeTasks` at once, at `BackgroundLow`. It uploads a transient G16 texture (LOD group `TEXTUREGROUP_Pixels2D`, never biased) and discards the CPU copy once the GPU has it.
  - **Each map is keyed by what it is made from**: the body, its relief (`SameRelief`), the sky's light for it (direction and angular radius) and the width. The sky checks every solid world's key each frame (`SyncShadowMaps`, outside transit) and re-bakes only a map whose key changed. A jump -- in-system or between stars -- re-bakes nothing that did not change, and `ds.Universe.ReloadPriors` re-bakes exactly the worlds whose relief moved. `RebuildFor` does not touch the maps. A changed or dropped map's bake is cancelled and detached, never waited on.
  - A map that lands **fades in** over `ShipSky::ShadowFadeSeconds` (1 s), so no world's shadow appears in one frame.
- **The lookup.** Both materials read the map through one Custom node over the shared file's `WR_ShadowMapCoord`, `WR_ShadowTapsAt` and `WR_Bilinear`. Eight texel loads give trilinear filtering at a level chosen from the pixel's footprint.
  - `SunShadowMap::Sample` is the C++ mirror, in double and in float.
  - The lookup's `Footprint` is **one pixel's** width. The faces' own footprint is `max(|ddx D|, |ddy D|) * filter_pixels` (2.0), so M_SkyBody and M_SkyGround hand the node that divided by `filter_pixels`. Otherwise level 0 would be read only where a texel covers two pixels, and the map would be read one mip coarser than *Measured while planning* states.
  - Under `PsiLo` the lookup is 0 (`WR_ShadowCoord::Night`), never row 0's value. Relief normals tilt far enough (about 40 degrees on IV) for sun-facing slopes past the terminator to have N.L > 0. The proof says those slopes are dark.
  - M_SkyGround blends the map into the vertex shadow by the same `Morph` that grows the relief in: `lerp(map, vertex, Morph)`. At the 50 km handover (Morph 0) the ground's shadow *is* the orbit's, and by the drive floor it is the tile's.
- **The switch.** `ds.Sky.Shadows` is the strength in both materials, `lerp(1, shadow, Shadows)`, so 0 draws the unshadowed look. Everything is baked, so the switch costs nothing either way.

**Amended after review (2026-09-28, second pass):** the plan was fixed against a review with two blocking findings and ten others. Each was checked against the tree before it was fixed.
- **Blocking, fixed:** every in-system jump re-baked every map, because `ShipNavState.cpp` bumps `JumpSerial` on an in-system arrival and `AShipSky::SyncTo` then calls `RebuildFor`. The maps are now keyed per world (Task 6, `.ShadowParameters`).
- **Blocking, fixed:** no test flew the ground with tile shadows on. `Eyes.ShadowBakeCost` now flies `GroundKeepsUp`'s scenario with them on (Task 7), and the flight has budgets.
- **Also fixed:**
  - Tile teardown no longer waits on in-flight builds (Task 4).
  - The claim that shade never steps at a tile's edge now holds only between tiles of the same level, and a 2:1 edge's step is measured (Task 4) and looked for (Task 10).
  - The lookup reads one pixel's footprint (Tasks 5, 6), and returns 0 under `PsiLo` (Task 5).
  - Memory is given per system, and measured rather than computed (Tasks 0, 7).
  - The dusk handover finds shaded ground before it shoots (Task 8).
  - Maps fade in (Task 6).
  - The tile and map switches are separate in test worlds (Task 4), and the bakes run at `BackgroundLow`.
  - The texture's LOD group is set, and the upload's hitch is timed (Tasks 6, 7).
  - The worst worlds of the start system and of the corpus are baked in the budgets (Task 7).
  - Task 6's `JsonNames` callers and the `sky_body`/`sky_ground` signatures are now shown, not described.

**Review status (2026-09-28):** rewritten for the ruling "cast shadows are baked, not marched". The per-pixel march of the first plan was spiked and measured NO-GO (+4 to +27 ms against a 1 ms budget). The ruling is in the spec's *Ruled on slice (b)'s build*. What the first plan built and learned is kept in *What was built and learned* below: the frames and timing baseline (Task 1), the spike's verdict (Task 1b), the gate tool, `Eyes.ReliefLook`'s still-pixel rule, `Eyes.LandingFrame`'s ABBA rounds and the exposure fix. The marched design's Tasks 2-6 are withdrawn. Their measurements that still bear on the bake are carried into *Measured while planning*.

**Tech Stack:** Unreal Engine 5.8.2 C++ (module `DeepSpace`); HLSL in material Custom nodes through the engine's `/Project` mapping; Python editor scripting (`Tools/setup_sky_materials.py`); automation tests through `./test.sh`, rendered and timed checks through `Tools/eyes.sh`, mutation proofs through `Tools/mutate.sh`.

**Spec:** `/home/matt/Development/deepspace/docs/superpowers/specs/2026-09-27-landing-design.md`, *Ruled on slice (b)'s build*, the bullets "Cast shadows are baked, not marched (2026-09-28)" and "Eyes captures now honour the game's exposure". The parent plan is `docs/superpowers/plans/2026-09-27-landing-slice-1.md`. Its *Conventions for every task* bind here, and so does its *RULINGS AFTER PLANNING* parity rule (the measured floor as a rule).

## Global Constraints

- **The ruling, verbatim:** "**Cast shadows are baked, not marched** (2026-09-28). A per-pixel march cost +4 to +27 ms at low sun against a 1 ms budget (the spike, NO-GO at N 12, 8 and 6). Because worlds do not spin and nothing orbits in this slice, the sun is fixed over every surface. So each ground tile computes its vertices' shadow while it is built, off the game thread, from the height function, and the orbital proxy reads a per-world shadow texture baked in C++ when the system loads. It costs nothing per frame. If worlds ever spin, the bake is redone as the sun moves."
- **One height function.** The march reads `IGroundField::Height`: `FReliefGround` over `FWorldRelief`, whose `Height` carries every band, the C++-only finer octaves and the craters (`Cratering` times `WR_CraterSum`). So **craters cast**, which answers the first plan's Task 0 question 2 by construction. Heights are physical and are never exaggerated.
- **One light.** The sun direction is the sky's own, `FSkyBodyView::LightDirection`: the unit vector from the body's centre to its star, universe axes. After Task 3 it comes from one function, `SkyProjection::LightDirection`, which `Project`, the tiles and the maps all call. The star's angular radius is `asin(R_star / d)`, clamped to `SunShadow::SunRadiusMax` (0.5 rad). No fill light is added: a shadow is as dark as the night side.
- **Where it applies.** Solid worlds only. Giants and oceans have no ground field, so they get no tile and no map. Their materials keep the default white map and cast nothing.
- **The handover.** At 50 km (Morph 0) M_SkyGround's shadow is the map, read through the same lookup code as M_SkyBody. `Eyes.HandoverParity` holds the ground to the orbit to 1e-3 of the mean with the shadow on, at a 3-degree dusk.
- **Parity follows the measured floor as a rule**, per footprint: the tolerance is 1e-3, or 1.25 x the float mirror's distance from double at that footprint, whichever is larger. A term over its allowance is a port bug. A FLOAT FLOOR verdict escalates, and the tolerance is never loosened.
- **The shared file** gains only the map's lookup (`WR_ShadowMapCoord`, `WR_ShadowTapsAt`, `WR_Bilinear`) and three shims. It stays in the scalar, no-swizzle subset, with every symbol `WR_`-prefixed and every literal `WR_REAL(...)`. A syntax either compiler refuses fails that compiler: `./build.sh` for C++, `Eyes.WorldReliefParity` for the GPU.
- **The frame must not rise.** `Eyes.LandingFrame`'s every case with the baked shadow on is held against Task 1's baseline (taken before any shadow existed) by `Tools/landing_frame_gate.py --not-rise`. That gate is GO only if no case's cost exceeds the medians' own error band (floored at 0.2 ms, the most two quiet baselines have differed by). The whole frame is still reported, not asserted: the spec's profiling owns the 16.6 ms.
- **The bake is measured and budgeted** (Task 7): per tile, per world, and the memory of each. The budgets are Task 0's ruling. The defaults the plan proceeds on are stated there.
- **Anti-chore:** nothing here is a timer, a charge or a state. A bake is background work the player never waits on. Before it lands, the world simply draws unshadowed.
- **A map is re-baked only when what it is made from changes** (the body's relief, the sky's light for it, the width). A jump changes none of these. Worlds do not spin and nothing orbits in this slice, so a map baked once serves every visit to its system while the ship is there. The sky's rebuild on a new jump serial rebuilds its proxies and leaves the maps alone.
- **Nothing here changes in one frame.** A map that lands mid-play fades its world's shadow in over `ShipSky::ShadowFadeSeconds` (1 s), which follows the sky's rule that nothing changes in a single frame. Test flushes land their maps already faded in, so frames that are judged are never caught mid-fade.
- **The game thread never waits on a bake.** Tile builds and map bakes are cancelled by flag and detached. Their lambdas capture no `this`, only shared, immutable inputs. They are never `Wait()`ed on outside `*ForTest` flushes. A detached task still counts against its cap until it finishes, so a teardown never oversubscribes the workers.
- **The workers:** 3 tile builds (`BackgroundNormal`, ruled 2026-09-28, from 2) and at most 2 map bakes (`BackgroundLow`, `ds.Sky.ShadowBakeTasks`), so 5 while a system's maps bake -- one over the 4 this plan was written to. When both queues wait, tiles go first.
- **All logic is C++ or the shared file.** The material graph only wires (ADR 0002). The material contract has three sides: `SkyMaterialContract.h`, `Tools/sky_material_contract.json` and the assets.
- **Tools and limits.**
  - Build only with `./build.sh`, test only with `./test.sh`, render and time only with `Tools/eyes.sh`, and mutate only with `Tools/mutate.sh`, all behind `Tools/ue_lock.sh`. The editor is closed.
  - Any local sweep uses at most 3-4 workers, under `nice -n 19`. The bake's own tasks default to 2, the terrain's cap.
  - Every test path is a sibling with no children. Every new test is proven able to fail.
- **The default suite.** This plan's additions to `./test.sh DeepSpace` total at most 8 s, with no test over the 5 s cap. Heavy measures live in `Eyes.ShadowBakeCost`, outside the suite. Test worlds (`SkyTestWorld::FSkyWorld`) build **without** tile shadows and map bakes unless asked, and the two are asked for separately: `EShadows::Tiles`, `EShadows::Maps`, or `EShadows::On` for both. So the suite's existing grounds cost what they did, and a test of the tiles never bakes 4096-column maps in the background.
- **Header changes** (`ShipSky.h`, `WorldGround.h`, `TerrainTile.h`, `WorldRelief.h`, `WorldReliefParams.h`) need `./rebuild.sh --force` before an editor session; `./build.sh` suffices for the tests. No `UPROPERTY`, component or `BlueprintImplementableEvent` is removed, so `check_blueprints.py` is not owed.

## Review Focus

These are the inputs no happy-path test meets, most likely first. Each is pinned in its owning task.

1. **The sun at the zenith and the nadir.** The azimuth is undefined there. The zenith is whole (the day exit, or the `CosT` guard), the nadir hidden (the night exit), and both are finite. Pinned by `DeepSpace.Surface.SunShadow.Exits` (**Task 2**).
2. **Smooth ground (1 cm of relief) and no ground (0 cm).** Smooth ground's only horizon is the sphere's: 0.5 with the sun's centre on the level, monotone, and the in-loop bound stops the march early. No ground casts nothing and reads nothing. Pinned by `.Exits` (**Task 2**).
3. **The exits meet the march with no step.** The night exit is a proof, and just inside it the march finds the disc hidden. The day exit rests on a sampled steepest slope with a 1.5 margin, and just inside it the real ground's march finds the disc whole. If that sample is wrong, the failure is a lit speck on the steepest slopes under a high sun. Pinned by `.Exits` and `.SteepestSlope` (**Task 2**).
4. **The map's seam at azimuth +-pi, its clamped rows and the point under the star.** Bilinear taps wrap in columns and clamp in rows, and the level rises as `cos(psi)` narrows the texels toward the pole. Pinned by `DeepSpace.Surface.SunShadowMap.Lookup` (**Task 5**), and on the GPU by `Eyes.WorldReliefParity`'s seam patch (**Task 8**).
5. **Rows the map leaves out are provably dark.** `PsiLo` is the dip from the highest peak to the lowest ground plus the star's radius. Pinned by `.Shape` and `.NightBelowTheMap` (**Task 5**).
6. **`ds.Sky.Shadows` out of range at the console** (7, -1, 0.5). It is clamped to 0..1, and 0 must draw the old look to compiler noise. Pinned by `DeepSpace.Sky.ShadowParameters` (**Task 6**) and by Task 10's check that the shadows-off frames are the before frames.
7. **A jump, a new system, a reload of the priors, or the end of play, mid-bake.**
   - A jump within the system (a new jump serial, the same system) re-bakes nothing.
   - A reload that moves a world's relief re-bakes that world alone.
   - A new system drops the old maps.
   - A cancelled bake stops within a column, and nothing waits for it.
   - Pinned by `.ShadowParameters` (**Task 6**).
8. **The light or the tile switch changing under a resident ground.** The ground rebuilds, no tile keeps a stale shadow, and the game thread never waits for the builds it drops. Pinned by `DeepSpace.Surface.GroundShadowLight` (**Task 4**).
9. **The cost lands where the frame cannot see it, and the suite does not pay it.** Pinned by `Eyes.ShadowBakeCost` (**Task 7**: the cold cut, the flight with tile shadows on, and a map's landing), `Eyes.LandingFrame --not-rise` (**Task 9**) and the suite timing (**Task 10**).
10. **A 2:1 tile edge.** A shared direction is shadowed at two footprints, and the finer side's odd vertices carry real values where the coarser side interpolates. So shade can step there even though skirts close the crack. Same-level edges are equal to the bit. The 2:1 step is measured by `.TileSunShadow` (**Task 4**), and looked for in Task 10's frames.
11. **Under the map's lowest row, and just above it.** Under `PsiLo` the lookup is 0. Just above it, the lowest row is read, clamped. Pinned by `.Lookup` (**Task 5**).

---

## Measured while planning

### Before the ruling (the marched plan, kept where it still bears)

**Physical relief casts little at the dusk goto's 10 degrees** (the first plan's Finding 1). Share of the ground shadowed:

| Share shadowed (dense truth) | 10 deg | 5 deg | 3 deg | 2 deg |
|---|---|---|---|---|
| III from 200 km | 0.0% | 0.0% | 0.3% | 6.3% |
| IV from 200 km | 0.3% | 19.3% | 43.3% | 56.0% |
| V from 200 km | 1.7% | 28.3% | 47.7% | 62.0% |
| IV at the ground | 2.0% | 30.7% | 46.7% | - |
| V at the ground | 5.3% | 42.0% | 59.3% | - |

That table was measured at the *pixel's* footprint. The bake's footprints are the tile's spacing on the ground and the map's texel from orbit, so the orbit's figures fall (below). The ruled frames at 10 degrees will therefore change little, and the 3-degree frames are where the shadow is seen.

**The per-pixel march is NO-GO** (Task 1b's verdict, below). That is why this plan bakes.

### For the bake (2026-09-28)

These were measured on a standalone build of `Shaders/Private/WorldRelief.ush`'s C++ half in double (`g++ -O2`, one core, `nice -n 19`), with the march exactly as Task 2 writes it, on Baemsekai III, IV and V's radius, peak, craters and star (`Saved/procgen_corpus.tsv`; the star is 0.16 R_sun, so it is 2.95, 1.66 and 0.97 degrees in radius from III, IV and V). Offsets and directions were random. The in-engine build may differ by a constant factor, and Task 6 measures there.

| What | Measured |
|---|---|
| One height, `FWorldRelief::Height`'s work | 0.11 us a simplex band; the crater sum 5.1 us at footprint 0, 3.4 us at 1/3000, 1.7 us at 1/100; a whole height 3.3-15 us by footprint (16 bands on IV) |
| A tile's vertices' shadow (33 x 33, IV-like, 12 samples, the day exit off) | heights alone 5-7 ms a tile; the shadow 16-70 ms. At a 10-degree dusk it is 48-60 ms (7.7-9.4 x the heights), at 3 degrees 16-47 ms (many vertices exit hidden), and at a 62.6-degree sun 63-70 ms without the day exit and about 0 with it. At 8 samples it is 16-45 ms. |
| The cold cut at 1.5 m (951 tiles, 2 tasks) | about 3 s today; about 27 s with the shadow at 12 samples (estimate: 951 x 56 ms / 2) |
| The geometric march against the dense truth (300 m and 100 m footprints) | at N 12, shaded ratio 0.79-1.03, mean \|dv\| 0.04-0.07 at 5 degrees and 0.10-0.14 at 2; at N 8, \|dv\| 0.07-0.14 at 5 and about 0.20 at 2; at N 16, 0.03-0.04 at 5 |
| Steepest sampled slope along the ground, \|grad S\| at footprint 0 (131,072 samples) | 13.6 at 12 bands, 14.0 at 16, 15.4 at 17, 16.7 at 20; the median 3.1-4.0; the craters' 2.30 (all six bands) |
| The day exit, `atan(SlopeScale x 1.5 x (16.72 + Cratering x 2.30)) + r` | III 14.4 deg, IV 42.4 deg, V 44.2 deg |
| `PsiLo`, the map's lowest row | III -4.83 deg, IV -5.21 deg, V -4.84 deg |
| A map at 4096 columns | about 1,084 rows (IV), 4.44 M texels, 8.9 MB at 16 bits plus 3 MB of mips; 14.6 s a world on one thread with the day exit off (rows above it need no heights, which roughly halves IV's and V's); 20 profile reads a texel |
| A map at 2048 columns | 542 rows, 1.11 M texels, 3.4 s a world, 3 MB |
| Solid worlds a system (`Saved/procgen_corpus.tsv`, 9,802 systems) | 3.75 on average, 16 at most |
| The worlds with the most relief for their size | the start system's Baemsekai I (0.68 Earth radii, 8.84 km) and II (0.74, 7.33 km); in the corpus, Baiti I (0.44, 9.74 km). The deepest `PsiLo`, so the most rows. |
| The corpus's largest solid world | Tishras I (2.13 Earth radii, 2.57 km): the most bands |

**Memory is per system, not per world.** A system's maps are all resident at once. The figures below are GPU memory at about 12 MB a world at 4096 columns, 47 MB at 8192 and 3 MB at 2048. A transient texture's mips keep their `BulkData` on the CPU after `UpdateResource` unless it is discarded. That has not been verified in engine source, and Task 7 measures it. It would double each figure, so Task 6 discards the copy once the render thread has it.

| Width | a world | a typical system (3.75 worlds) | the worst system (16) |
|---|---|---|---|
| 2048 | 3 MB | 11 MB | 48 MB |
| 4096 | 12 MB | 45 MB | 190 MB |
| 8192 | 47 MB | 176 MB | 750 MB |

The resident cut's vertex shadows are also kept twice: once in `FResident::Tile` and once in `UTerrainTileComponent::Kept` (`WorldGround.cpp` sets `bKeepForTest` on every tile, for proxy recreation). 951 tiles x 1,089 floats x 2 copies is 8.3 MB, not 4.1.

**A texel cannot be under a pixel wherever the proxy draws.** The table below is the lookup's resolution *as Task 6 wires it*. The node is handed one pixel's footprint: the faces' `max(|ddx D|, |ddy D|) * filter_pixels`, divided by `filter_pixels` (2.0). So level 0 is read wherever a texel covers a pixel or more. The first draft handed the node the filtered footprint. Level 0 would then have been read only where a texel covers two pixels, and every threshold below would halve: at 4096 columns, about 17 degrees rather than 35, which is the edge of the ~18-degree opening frame. The proxy draws from 50 km up. At the game's 103-degree FOV on a 4K screen a pixel is 6.55e-4 rad at the centre, so a texel of `2 pi R / Width` is under a nadir pixel only above `h = (2 pi / Width) / 6.55e-4 x R`:

| Width | h_min / R | the world is then this wide | III texel, h_min | IV texel, h_min | V texel, h_min | IV at 200 km | IV at 50 km |
|---|---|---|---|---|---|---|---|
| 2048 | 4.69 | 20.3 deg | 27.1 km, 41,400 km | 17.5 km, 26,800 km | 16.5 km, 25,200 km | 134 px a texel | 536 |
| 4096 | 2.34 | 34.8 deg | 13.6 km, 20,700 km | 8.8 km, 13,400 km | 8.3 km, 12,600 km | 67 | 268 |
| 8192 | 1.17 | 54.8 deg | 6.8 km, 10,400 km | 4.4 km, 6,700 km | 4.1 km, 6,300 km | 34 | 134 |

A texel under a pixel at 200 km would take about 68,000 columns (2.8e10 texels), and at 50 km about 274,000. No per-world texture reaches that. So the orbit's map is finer than the screen while the world is under 20-55 degrees across, which includes the opening frame's ~18-degree world at every width above. Nearer than that, the map is magnified: its shadows are soft at the texel's scale, never aliased. Below 50 km the tiles take over, blended in by Morph, with shadows at their vertex spacing. On the ground, at 4K and the game's FOV, a drawn tile's vertices are 12-24 pixels apart (CDLOD split factor 2: a tile is drawn 2-4 edges away), so a vertex shadow's edge is spread over that many pixels. Where a tile meets a neighbour one level coarser, the shared directions are shadowed at two footprints. The finer side's odd edge vertices carry their own values, where the coarser side interpolates. So the shade can step along a 2:1 edge even though the skirts hide the crack in the geometry. Task 4 measures that step. **This is the ruling's cost, and it goes to the developer as Task 0's first question.**

---

## What was built and learned (kept)

**Task 1 (done, `fbf172d`, amended in `4710b9a`, `5018fe2`, `3aacdc7`): the frames before and the timing baseline.** This built:

- `DeepSpace.Sky.GotoDuskElevation`: goto dusk takes the star's elevation.
- `Eyes.ReliefLook`: III, IV and V from 200 km and at 1.5 m, at the dusk goto's 10 degrees and at 3, with `ds.Sky.Shadows` 0 and 1 when it exists.
  - The ruled frames are drawn at the capture's default. The measured *read* frames are drawn at the game's exposure plus `ReadStops`, and read only on pixels two captures without the term held within 1 LSB. They carry the coverage, the flicker as noise, and aliasing against 2x (`alias_off`, `alias_off2`, `alias_on`).
- `Eyes.LandingFrame`: six cases (`50km`, `1.5m`, `50km_dusk10`, `1.5m_dusk10`, `1.5m_dusk3`, `200km_dusk10`) at 4K, ABBA after one untimed warm-up round, with medians, IQR and raw times, each case's sun and `agl_m`, and `switch present|absent`. The switch's proof is a separate 1080p capture at the read exposure, read on held pixels.
- `Tools/relief_look_compare.py` and `Tools/landing_frame_gate.py`, each with its tests. The gate reads twice the medians' combined standard error, floored at `RUN_TO_RUN_MS` 0.2 ms.
- `Tests/Eyes/EyesFrames.h`, whose `Expose` persists a view state so captures honour `ds.Sky.Exposure`: the exposure fix.

The before runs are in `Saved/Eyes/ReliefLook/shadows-before` and `-2`, and `Saved/Eyes/LandingFrame/baseline` and `-2`; the first versions are kept as `*-v1`. **Those are this plan's "before".** Nothing in Tasks 2-5 touches a shader, so they stay valid until Task 6.

**Task 1b (done, uncommitted, thrown away): the spike.**

**Spike verdict (2026-09-28, track T, RTX 4070 Ti SUPER at 4K):** `NO-GO -- at N = 6 the worst case is 1.5m_dusk10 at +11.71 ms (spread 0.55); early exit tried: +13.07 ms (k = 6 coarse bands, the finer ones bounded by SimplexValueBound x weight / frequency -- looser than the saving)`. Tried: `N 12: worst +26.99 ms (1.5m_dusk10), N 8: worst +17.07 ms (1.5m_dusk10), N 6: worst +11.71 ms (1.5m_dusk10), N 6 + early exit: worst +13.07 ms (1.5m_dusk10)`. Per case at N 12 / 8 / 6: 50km_dusk10 +11.14 / +6.96 / +5.12, 200km_dusk10 +10.60 / +5.97 / +3.78, 1.5m_dusk3 +14.13 / +10.29 / +8.54, 1.5m_dusk10 +26.99 / +17.07 / +11.71; the start's two cases (sun 62.6 degrees, above the exit) +0.1 to +0.5, within their spread. Baseline (before the term): 50km 10.63, 1.5m 18.87, 50km_dusk10 9.39, 1.5m_dusk3 15.66, 200km_dusk10 12.87, 1.5m_dusk10 16.07 ms; a second baseline read every cost within 0.2 ms of zero. Nothing was committed from the spike.

**Found while taking the baseline** (Task 1, as committed): the ground frames are not deterministic between two captures of a still scene -- the far tiles' skirts and seams flicker frame to frame (the near ground is identical) -- so `coverage` with no term at all reads 1-10% at the ground and up to 100% on the near-black 3-degree frames. The spike's runs, which write no `Shadows`, read 21% (N 12), 89% (N 8), 69% (N 6) and 51% (N 6 with the early exit). No threshold on that measure tells a working switch from a missing one, hence the held-pixel rule below. A GPU pipeline still compiling after `FinishAllCompilation` drew III's ground red in the first pass (fixed: both tests settle in wall-clock time). And `1.5m_dusk10`, taken straight after `50km_dusk10`, drew every frame slower than the last (150 ms to 1.4 s) and slowed the cases after it; taken last it is 16 ms. Neither is diagnosed.

**Amended after review (2026-09-28): the measures, re-taken.** A review of Task 1 found every rendered measure it built reading noise. Fixed on the track, and the before runs re-taken with the fixed tests (`Saved/Eyes/ReliefLook/shadows-before`, `-2`; `Saved/Eyes/LandingFrame/baseline`, `-2`; the first versions kept as `*-v1`):

- **Scene captures had no exposure.** A `USceneCaptureComponent2D` without a view state applies none: moving `ds.Sky.Exposure`, or a bias on the capture itself, 14 stops changed not one pixel. Every Eyes frame so far -- the ruling's 42.9 -> 17.3 among them -- was drawn at that default, not at the game's 0.7 EV100. `Tests/Eyes/EyesFrames.h` (`Expose`) persists a view state for the frames that are measured, and draws them at the game's exposure plus `ReadStops` whole stops, metered with `EYES_METER=1`. **At the game's own exposure every 3-degree frame, and IV's ground at 10, has under 2% of the frame lit: the game shows them black too.** That is a finding for the developer beside Task 0's first question. The ruled frames (`_shadows0/1.png`, `off_mean`, `on_mean`) are kept at the capture's default, so the ruling's numbers still compare.
- **Measures are read where two captures held still.** Each view is drawn at the read exposure twice without the term and once with it; `coverage` is read only on pixels the two without it held within 1 LSB, with the unmasked `flicker` printed as the noise, and aliasing is read on the same pixels for both captures without the term (`alias_off`, `alias_off2`) and with it. Task 6's aliasing flag is `alias_on` over 1.5x `alias_off` and by more than max(0.1, 3 x |`alias_off2` - `alias_off`|).
- **The off-frame identity.** Two runs also differ by whole tiles (which are resident, at which level): before the term, 1.2-6.2% of the pixels both runs held moved by more than 1 LSB, the rest matched within 1 LSB with a mean-luma gap up to 0.09, and the orbits matched exactly. `relief_look_compare.py` therefore allows at most 10% moved and a quarter level of mean luma on the rest; two before runs pass, and a 3% darkening of one frame fails. It fails outright on a frame missing from either run, too little held, a frame too dark to read (under 5% lit) or a changed `read_stops`.
- **The gate reads the medians' error.** 2x the per-frame IQR was wider than the 1.0 ms budget, so a free term was never GO: the first baselines, costs -0.18..+0.15 ms, read UNDECIDED. The band is now twice the combined standard error of the two medians (sigma robustly from the raw `times_on`), floored at 0.2 ms, the most two quiet baselines differed by; empty or missing reports are UNDECIDED. The first baselines against each other now read GO; the spike at N 6 still NO-GO. A run disturbed by other load on the machine (transients of 50-200 ms over several rounds) reads UNDECIDED, as it should: re-run it quiet.
- **LandingFrame:** one untimed round before the ABBA rounds (the first timed frame was 3-5 ms slow, always in the off slot); `agl_m` over the ground itself (the 1.5 m cases read 2.96 m and 2.62 m: the ground's hard stop lifts the gear off the slope, which the datum-relative figure hid); `switch present|absent` on every line and a warning when `ds.Sky.Shadows` is missing. **The switch's proof** is a separate 1080p capture at the read exposure (5 stops at `1.5m_dusk3`), read on held pixels: at least 1% of the frame lit and held, and at least **10%** of that taken under half. With no switch it reads 0.5-2.3% there (`switch_cov`; 0.5-2.3% in the quiet baselines), against planning's 40-60% for the term.

**The first plan's Task 0** was never answered, because the spike ran first. Its first question, the dusk elevation, is answered by this plan's done-when: the frames are taken at the dusk goto's 10 degrees and at 3. Its second, whether craters cast, is answered by the design: the march reads `FWorldRelief::Height`, craters included. Its third, the per-pixel budget, is answered by the ruling. This plan's own questions are Task 0 below.

---

## Execution: tree, order, ownership

Track T owns this work. From the spec's *Parallel tracks*, T owns `TerrainQuadtree.*`, `TerrainTile.*`, `WorldGround.*`, `SkyProjection.*`, `ShipSky.*`, `setup_sky_materials.py`, the contract, and `Surface/WorldRelief.*` in slice (b). For this work T also takes:

- the new `Surface/SunShadow.*` and `Surface/SunShadowMap.*`, and `TerrainTileComponent.*`;
- `Tests/SkyTestWorld.h` (the `EShadows` switch);
- `Tests/Eyes/LandingFrameEyesTest.cpp`, which the orchestrator wrote in Task 39 (Z);
- a paragraph of CLAUDE.md, and the spec's measured line.

The work runs in `/home/matt/Development/deepspace/.worktrees/landing-b-t` on `feat/landing-b-t`, which has merged `feat/landing-b` and main (`923ae4b`). The orchestrator merges the track into `feat/landing-b` when Task 10 lands, and nothing on the track is merged into main by an agent. The review range for this work is `923ae4b..feat/landing-b-t`.

```
Task 0  STOP: the developer rules on the map's resolution, the tiles' cost and the bake's budgets   -- Tasks 2-5 may run meanwhile
Task 2  SunShadow: the pure horizon march, its exits, its schedule; known values
Task 3  the sky's light, one function: SkyProjection::LightDirection, StarAngularRadius, SunLightOf
Task 4  the tiles carry the shadow: TerrainTile::Build, UV0, AWorldGround's light and switches
Task 5  SunShadowMap: the orbit's map, pure -- the bake, the levels, the lookup in the shared file
Task 6  the materials, the contract, and the sky's bakes at system load                          -- needs Task 0's answers
Task 7  THE COST, measured and budgeted: Eyes.ShadowBakeCost                                     -- against Task 0's budgets
Task 7b (only if a budget is over) a stop-and-report point
Task 8  parity: the map on the GPU (Eyes.WorldReliefParity), the handover at dusk (Eyes.HandoverParity)
Task 9  the frame must not rise: Eyes.LandingFrame against the baseline, --not-rise; the switch proven by pixels
Task 10 the frames after, the numbers, the docs, the suite's time
```

Task 2 comes first, and Tasks 3 and 5 need only it (all three are pure). Task 4 needs 2 and 3, Task 6 needs 3, 4 and 5, and Task 7 needs 6. They are committed one task at a time, in the order above, on the one branch.

**First, in the tree:**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git status --short && git log --oneline -1 && ls Saved/Eyes/ReliefLook/shadows-before/report.txt Saved/Eyes/LandingFrame/baseline/report.txt
```

Expected: a clean tree at `923ae4b` (or later), and both before reports present. **If either before report is missing**, take it again from the tree as it stands now, before Task 6 changes a shader: `EYES_TAG=shadows-before Tools/eyes.sh Eyes.ReliefLook` and `EYES_TAG=baseline Tools/eyes.sh Eyes.LandingFrame`. The assets are unchanged since Task 1, so these are still the before frames.

---

## Task 0: STOP -- three questions for the developer

**RULED, 2026-09-28** (the spec's "The cast-shadow plan's Task 0 and Task 7b, ruled"):
1. **The map's resolution:** the defaults stand -- 4,096 columns -- but the orbital maps get a **per-system GPU memory cap of 128 MB**, a system with many worlds lowering its maps' resolution to fit. Built as `ShipSky::CappedShadowWidths` (the finest texel halved first, never under 256 columns) over `ShipSky::ShadowTextureBytes`, the RHI's own size for each texture. See Task 7b's record.
2. **The tiles' cost:** every tile vertex, 12 samples, and **three background workers** (`ds.Terrain.BuildTasks` 3, from 2).
3. **The budgets:** as below, with `system_gpu_mb` 128 MB for every system, the corpus's worst included, and a map's landing uploaded **in pieces on the render thread over several frames**, so `map_land_ms` (4 ms) is a frame's, not a map's.

**Owner:** the orchestrator. **Depends on:** nothing. This is the spec approval gate, not an executable task. Put these to the developer in one form (`AskUserQuestion`), with the numbers. Tasks 2-5 are pure and may run meanwhile. Task 6 (the materials) does not start before the answers, and Task 7 holds the cost to the budgets they set.

1. **The orbit's map cannot be finer than a pixel where the proxy draws.** The proxy draws from 50 km up, and a texel under a 4K pixel there would take about 274,000 columns (*Measured while planning*). Each option below states:
   - its texel on IV;
   - where that texel is under a pixel, with the node handed one pixel's footprint (Task 6 divides the faces' `filter_pixels` out; without that, every angle below would halve);
   - the bake per world on one thread;
   - GPU memory per world, for a typical system (3.75 solid worlds) and for the worst (16).

   The options:
   - (a) **4096 columns** (recommended). The texel is 8.8 km on IV (13.6 on III, 8.3 on V). The map is finer than the screen while the world is under 35 degrees across, which includes the ~18-degree opening frame. Below that the shadows are soft at 8.8 km: a 67-pixel texel at 200 km. The bake takes about 8-15 s a world. Memory is 12 MB a world, about 45 MB for a typical system and about 190 MB for the worst.
   - (b) 8192 columns. The texel is 4.4 km, finer than the screen under 55 degrees. About 30-60 s a world. 47 MB a world, about 176 MB typical and about **750 MB** for the worst system.
   - (c) 2048 columns. The texel is 17.5 km, finer than the screen under 20 degrees, which is only just the opening frame. About 2-4 s a world. 3 MB a world, 11 MB typical, 48 MB worst.
   - (d) A second, local map around the ship, re-baked as it moves. It is the only way to a texel under a pixel below a few thousand km, but it is not "baked when the system loads" and it has a per-frame decision in it: a new ruling.

   `ds.Sky.ShadowMapWidth` (a power of two, 256-8192) is read at each system load, so a playtest can try another width without a rebuild.
2. **The tiles' cost.** The shadow at 12 samples costs about 8x a tile's heights at dusk (48-60 ms a tile against 5-7 ms). The cold cut at 1.5 m would take about 27 s to be resident instead of about 3 s. Coarse tiles come first, and a missing tile's parent is drawn, so there is never a hole, only a later sharp ground. Options:
   - (a) **Accept it** (recommended): the ruled design, every vertex, 12 samples. Budget: the cold cut within 30 s and a tile at most 10x.
   - (b) Also raise `ds.Terrain.BuildTasks` from 2 to 3 (the machine's cap): about 18 s.
   - (c) Mark every other vertex and interpolate: about 4x cheaper, about 9 s, with shadow detail at two vertex spacings (24-48 px at 4K). This is a coarsening, and so a ruling.
   - (d) 8 samples: about 5x. At 5 degrees this fails the march's quality bound (mean |dv| 0.07-0.14 against 0.10), so it is not offered alone.
   - (e) A day exit that depends on the footprint: `SteepestSlope(Params, FootprintCm)` bounds only the bands that have not yet faded at the tile's spacing. The finest bands are what set the 16.72, so a coarse tile's exit falls much lower, and mid-sun tiles cost far less. It is as safe as today's exit, and it changes no value: a vertex over its exit is whole either way. It needs each band's sampled gradient measured, as `.SteepestSlope` measures the sum. It is offered as Task 7b's first remedy, not built now.

   At noon on IV the day exit makes the shadow cost almost nothing. The cost is at dusk. **The cost also bites in flight, not only at a cold start.** At about 8-10x a tile, two tasks build about 40 tiles a second where they built about 400. That risks the drawn ground under the ship falling behind (GearClearance / 10, `DeepSpace.Surface.GroundKeepsUp`), and the wait for the coarse cut before the 50 km handover. Task 7 flies that scenario with the shadow on, against budgets.
3. **The bake's budgets**, as Task 7 will hold them (recommended defaults):
   - the cold cut at 1.5 m over IV at a 10-degree dusk resident within **30 s** on 2 tasks;
   - a tile's median build with the shadow at most **10x** without;
   - a world's map at most **15 s** on one thread and **16 MB** (level 0 and its mips), for the slowest and largest of the start system's five worlds and of the corpus's two extremes;
   - a system's maps at most **64 MB** of GPU memory resident for the start system, measured through the engine (`CalcTextureMemorySizeEnum(TMC_ResidentMips)`), with **0** bytes left on the CPU once uploaded; the corpus's worst system is reported (about 190 MB at 4096), not held;
   - a map's landing at most **4 ms** of game-thread time (the texture's creation and upload call);
   - a ground's release with a full set of shadowed builds in flight at most **2 ms** of game-thread time. Release happens on every fold opened near a world, on leaving prefetch range, and on every change to `ds.Terrain.Shadows` or `ShadowSamples`;
   - in flight with tile shadows on (`GroundKeepsUp`'s scenario, paced to the wall clock): **no** frame under 1 km without drawn ground under the ship, the worst drawn gap within **GearClearance / 10**, and the coarse cut resident within **10 s** of arriving under 51 km;
   - the resident cut's kept vertex shadows at most **12 MB** of CPU memory, measured from the arrays themselves. Both copies count, about 8.3 MB. The first draft's 8 MB budget was a formula over one copy.
   - `Eyes.LandingFrame` **not rising** in any case.

   Also for the developer's information, not a question:
   - The day exit rests on a *sampled* steepest slope with a 1.5 margin, as the marched plan's did. A slope steeper than any sample would show as a lit speck on it under a high sun.
   - Shade may step at a 2:1 tile edge (Task 4 measures by how much). Task 10 looks for it in the frames.
   - A map that lands fades its world's shadow in over 1 s. Until it lands the world draws unshadowed, the opening world included, for up to a world's bake time.

Write the answers into the spec (on the track) as a sub-bullet of the ruling "Cast shadows are baked, not marched", and amend this plan where they change it: `SunShadowMap::DefaultWidth` in Task 5, the budget constants in Task 7, and `ds.Terrain.BuildTasks` or the vertex stride in Task 4. **If a first form already went to the developer with the first draft's numbers**, send the corrections with the next one: the per-system memory, the flight's budgets, and the 8 MB budget that was one copy. Until the answers come, Tasks 2-5 proceed on the recommended defaults: 4096 columns, every vertex, 12 samples.

---

## Task 2: `SunShadow` -- the pure horizon march

**Owner:** T. **Depends on:** nothing.

**Files:**
- Create: `Source/DeepSpace/Surface/SunShadow.h`, `Source/DeepSpace/Surface/SunShadow.cpp`
- Create: `Source/DeepSpace/Tests/SunShadowTest.cpp` (`DeepSpace.Surface.SunShadow.KnownValues`, `.Exits`, `.Schedule`, `.AgainstProfile`, `.SteepestSlope`)

**Interfaces:**
- Consumes: `IGroundField` (`Surface/GroundField.h`: `RadiusCm`, `Height`, `MaxHeightCm`, `MinHeightCm`, `MaxSlope`), `FReliefGround`, `FWorldRelief::{SlopeScale, DetailSum, CraterSum, GetDetailFrequencies}`, `FWorldReliefParams`, `GroundFixtures::FixtureParams` (`Tests/GroundFixtures.h`).
- Produces:

```cpp
namespace SunShadow
{
    struct FSunLight { FVector3d Direction; double AngularRadius; bool IsSet() const; };
    struct FSunVisibility { double Visible; double Horizon; int32 Reads; };
    inline constexpr double SunRadiusMax = 0.5;
    inline constexpr double NearestCm = 250.0;
    inline constexpr double FootprintFactor = 2.0;
    inline constexpr int32 DefaultSamples = 12;
    inline constexpr double DetailGradientSampled = 16.72;
    inline constexpr double CraterGradientSampled = 2.30;
    inline constexpr double SteepestMargin = 1.5;
    double DiscAbove(double X);
    double Elevation(double RadiusCm, double HereCm, double ThereCm, double Arc);
    double MarchEnd(double RadiusCm, double HereCm, double PeakCm, double Lower);
    double NightDip(double RadiusCm, double HereCm, double LowestCm);
    FSunVisibility Visible(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm,
                           double SteepestSlope, int32 Samples = DefaultSamples, TOptional<double> HereCm = {});
    FSunVisibility AlongProfile(double RadiusCm, double PeakCm, double LowestCm, double SteepestSlope,
                                TConstArrayView<double> Heights, double Step, double SunElevation, double SunRadius);
    double SteepestSlope(const FWorldReliefParams& Params);
}
```

- [ ] **Step 1: Write the failing tests.** Create `Source/DeepSpace/Tests/SunShadowTest.cpp`:

```cpp
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/WorldRelief.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowKnownValuesTest, "DeepSpace.Surface.SunShadow.KnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowExitsTest, "DeepSpace.Surface.SunShadow.Exits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowScheduleTest, "DeepSpace.Surface.SunShadow.Schedule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowAgainstProfileTest, "DeepSpace.Surface.SunShadow.AgainstProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowSteepestSlopeTest, "DeepSpace.Surface.SunShadow.SteepestSlope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SunShadowTestLocal
{
    using namespace SunShadow;

    /** Ground that is a plane tilted K (rise over run) up toward Up, as far
     *  as a sphere allows: Height(D) = K R (D . Up). Where D is square to Up
     *  its slope along the ground is K exactly, and nowhere more. */
    class FTiltedGround final : public IGroundField
    {
    public:
        FTiltedGround(double InRadiusCm, double InK, const FVector3d& InUp) : R(InRadiusCm), K(InK), Up(InUp) {}
        virtual double RadiusCm() const override { return R; }
        virtual double Height(const FVector3d& D, double) const override { return K * R * FVector3d::DotProduct(D, Up); }
        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            Grad = Up * (K * R);
            return Height(D, FootprintCm);
        }
        virtual double OmittedBoundCm(double) const override { return 0.0; }
        virtual double MaxHeightCm() const override { return K * R; }
        virtual double MinHeightCm() const override { return -K * R; }
        virtual double MaxSlope() const override { return K; }

    private:
        double R;
        double K;
        FVector3d Up;
    };

    /** A smooth sphere, its heights bounded +-BoundCm: ground with nothing
     *  on it. A bound of 0 is no ground at all, an ocean's or a giant's. */
    class FSmoothGround final : public IGroundField
    {
    public:
        FSmoothGround(double InRadiusCm, double InBoundCm) : R(InRadiusCm), Bound(InBoundCm) {}
        virtual double RadiusCm() const override { return R; }
        virtual double Height(const FVector3d&, double) const override { return 0.0; }
        virtual double HeightAndGradient(const FVector3d&, FVector3d& Grad, double) const override
        {
            Grad = FVector3d::ZeroVector;
            return 0.0;
        }
        virtual double OmittedBoundCm(double) const override { return 0.0; }
        virtual double MaxHeightCm() const override { return Bound; }
        virtual double MinHeightCm() const override { return -Bound; }
        virtual double MaxSlope() const override { return 0.0; }

    private:
        double R;
        double Bound;
    };

    /** Every height asked of Inner, in order. */
    class FRecordingGround final : public IGroundField
    {
    public:
        struct FRead
        {
            FVector3d D;
            double FootprintCm;
        };
        mutable TArray<FRead> Reads;

        explicit FRecordingGround(const IGroundField& InInner) : Inner(InInner) {}
        virtual double RadiusCm() const override { return Inner.RadiusCm(); }
        virtual double Height(const FVector3d& D, double FootprintCm) const override
        {
            Reads.Add({ D, FootprintCm });
            return Inner.Height(D, FootprintCm);
        }
        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            return Inner.HeightAndGradient(D, Grad, FootprintCm);
        }
        virtual double OmittedBoundCm(double FootprintCm) const override { return Inner.OmittedBoundCm(FootprintCm); }
        virtual double MaxHeightCm() const override { return Inner.MaxHeightCm(); }
        virtual double MinHeightCm() const override { return Inner.MinHeightCm(); }
        virtual double MaxSlope() const override { return Inner.MaxSlope(); }

    private:
        const IGroundField& Inner;
    };

    /** The star Elevation rad above D's level, toward Toward's part along it. */
    FSunLight LightAt(const FVector3d& D, const FVector3d& Toward, double Elevation, double AngularRadius)
    {
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        FSunLight Sun;
        Sun.Direction = (D * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
        Sun.AngularRadius = AngularRadius;
        return Sun;
    }

    /** A seed offset as the sky's are: three multiples of 1/256 under 256. */
    FVector3d OffsetFrom(FRandomStream& Stream)
    {
        const int32 X = Stream.RandRange(0, 65535);
        const int32 Y = Stream.RandRange(0, 65535);
        const int32 Z = Stream.RandRange(0, 65535);
        return FVector3d(X, Y, Z) / 256.0;
    }

    /** The truth the geometric march approximates: the heights at every
     *  Step = Footprint / R toward the star out to the march's end, read at
     *  the footprint, and AlongProfile over them -- the orbit map's own
     *  schedule. */
    FSunVisibility Dense(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm, double Steepest)
    {
        const double R = Ground.RadiusCm();
        const double SinT = FVector3d::DotProduct(Sun.Direction, D);
        const FVector3d Level = Sun.Direction - D * SinT;
        const FVector3d Toward = Level.GetSafeNormal();
        const double Theta = FMath::Atan2(SinT, Level.Size());
        const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunRadiusMax);
        const double Here = Ground.Height(D, FootprintCm);
        const double End = MarchEnd(R, Here, Ground.MaxHeightCm(), Theta - Radius);
        const double Step = FootprintCm / R;
        TArray<double> Heights = { Here };
        for (int32 K = 1; K * Step <= End; ++K)
        {
            const double Arc = K * Step;
            Heights.Add(Ground.Height(D * FMath::Cos(Arc) + Toward * FMath::Sin(Arc), FootprintCm));
        }
        return AlongProfile(R, Ground.MaxHeightCm(), Ground.MinHeightCm(), Steepest, Heights, Step, Theta, Sun.AngularRadius);
    }
}

bool FSunShadowKnownValuesTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // The disc above a straight horizon.
    TestTrue(TEXT("a horizon a radius under the disc's centre leaves it whole"), DiscAbove(-1.0) == 1.0);
    TestTrue(TEXT("one through its centre leaves half"), FMath::IsNearlyEqual(DiscAbove(0.0), 0.5, 1e-15));
    TestTrue(TEXT("one a radius over it hides it"), DiscAbove(1.0) == 0.0);
    TestTrue(TEXT("and past either edge it stays there"), DiscAbove(-7.0) == 1.0 && DiscAbove(7.0) == 0.0);
    // (acos x - x sqrt(1 - x^2)) / pi. The + form is symmetric and falling
    // too, and agrees at -1, 0 and 1; it gives 0.4712 at 0.5.
    TestTrue(FString::Printf(TEXT("half a radius over the centre leaves 0.195501 (%.12f)"), DiscAbove(0.5)),
        FMath::IsNearlyEqual(DiscAbove(0.5), 0.19550110947788538, 1e-12));
    TestTrue(FString::Printf(TEXT("half a radius under it, 0.804499 (%.12f)"), DiscAbove(-0.5)),
        FMath::IsNearlyEqual(DiscAbove(-0.5), 0.8044988905221148, 1e-12));
    TestTrue(FString::Printf(TEXT("a fifth over it, 0.373530 (%.12f)"), DiscAbove(0.2)),
        FMath::IsNearlyEqual(DiscAbove(0.2), 0.373530039052331, 1e-12));

    // The elevation: the triangle of the two points and the centre, exactly.
    TestTrue(FString::Printf(TEXT("level ground dips away by half the arc (%.15f, %.15f)"), Elevation(1.0e8, 0.0, 0.0, 0.01), Elevation(1.0e8, 0.0, 0.0, 0.3)),
        FMath::IsNearlyEqual(Elevation(1.0e8, 0.0, 0.0, 0.01), -0.005, 1e-15) && FMath::IsNearlyEqual(Elevation(1.0e8, 0.0, 0.0, 0.3), -0.15, 1e-15));
    TestTrue(FString::Printf(TEXT("1 km up, 10 km off, over a 1,000 km world stands 0.0946183 rad up (%.15f)"), Elevation(1.0e8, 0.0, 1.0e5, 0.01)),
        FMath::IsNearlyEqual(Elevation(1.0e8, 0.0, 1.0e5, 0.01), 0.0946183473562076, 1e-13));

    // The march's end, acos(rho cos L) - L: here rho is cos 0.1.
    const double R = 1.0e8;
    const double Peak = R * (1.0 / FMath::Cos(0.1) - 1.0);
    TestTrue(FString::Printf(TEXT("with the sun's edge on the level the march ends 0.1 rad off (%.15f)"), MarchEnd(R, 0.0, Peak, 0.0)),
        FMath::IsNearlyEqual(MarchEnd(R, 0.0, Peak, 0.0), 0.1, 1e-12));
    TestTrue(FString::Printf(TEXT("with it 0.05 under, 0.161766 rad off (%.15f)"), MarchEnd(R, 0.0, Peak, -0.05)),
        FMath::IsNearlyEqual(MarchEnd(R, 0.0, Peak, -0.05), 0.16176609378183154, 1e-12));
    TestTrue(TEXT("and there the highest ground stands exactly at the sun's lower edge"),
        FMath::IsNearlyEqual(Elevation(R, 0.0, Peak, MarchEnd(R, 0.0, Peak, -0.05)), -0.05, 1e-12));
    TestTrue(TEXT("from the peak itself, with the edge up, there is nothing to look for"), FMath::Abs(MarchEnd(R, Peak, Peak, 0.3)) <= 1e-12);

    // The night's dip, to the lowest ground's tangent.
    TestTrue(FString::Printf(TEXT("1 km over a 1,000 km world whose lowest ground is 1 km down dips 0.0632245 rad (%.15f)"), NightDip(1.0e8, 1.0e5, -1.0e5)),
        FMath::IsNearlyEqual(NightDip(1.0e8, 1.0e5, -1.0e5), 0.06322448399238306, 1e-13));
    TestTrue(TEXT("and none from the lowest ground itself"), NightDip(1.0e8, -1.0e5, -1.0e5) == 0.0);

    // A plane tilted 5 degrees up toward the star: the horizon is the slope's
    // own, atan K, to the first sample's curve (under 1e-6 rad).
    const double Radius = 0.03;
    const FVector3d D(1.0, 0.0, 0.0);
    const FVector3d Up(0.0, 1.0, 0.0);
    const FTiltedGround Tilted(6.0e8, FMath::Tan(FMath::DegreesToRadians(5.0)), Up);
    const double Slope = FMath::Atan(Tilted.MaxSlope());
    struct FCase { double Over; double Seen; };
    for (const FCase& Case : { FCase{ 0.5, 0.8044988905221148 }, FCase{ 0.0, 0.5 }, FCase{ -0.5, 0.19550110947788538 } })
    {
        const double Theta = Slope + Case.Over * Radius;
        const FSunVisibility Seen = Visible(Tilted, D, LightAt(D, Up, Theta, Radius), 100.0, Tilted.MaxSlope());
        TestTrue(FString::Printf(TEXT("uphill, the sun's centre %.1f radii over the slope shows %.6f of it (%.8f)"), Case.Over, Case.Seen, Seen.Visible),
            FMath::Abs(Seen.Visible - Case.Seen) <= 1.0e-4);
        TestTrue(FString::Printf(TEXT("its horizon is the slope's, atan K (%.3e off)"), Seen.Horizon - Slope), FMath::Abs(Seen.Horizon - Slope) <= 1.0e-6);
        TestTrue(TEXT("and what it sees is the disc above that horizon"), FMath::IsNearlyEqual(Seen.Visible, DiscAbove((Seen.Horizon - Theta) / Radius), 1e-12));
    }
    const FSunVisibility Downhill = Visible(Tilted, D, LightAt(D, -Up, Slope, Radius), 100.0, Tilted.MaxSlope());
    TestTrue(FString::Printf(TEXT("downhill, the same sun is whole (%.8f)"), Downhill.Visible), Downhill.Visible == 1.0);

    // A profile: a wall 64.5 m high three steps (3 km) off over a 1,000 km
    // world, the sun 0.02 rad up and 0.01 in radius: the wall's top is all
    // but on the sun's centre.
    const TArray<double> Wall = { 0.0, 0.0, 0.0, 6450.0, 0.0, 0.0, 0.0, 0.0 };
    const FSunVisibility Profiled = AlongProfile(1.0e8, 1.0e4, -1.0e4, 1.0e3, Wall, 0.001, 0.02, 0.01);
    TestTrue(FString::Printf(TEXT("the wall is the horizon, 0.0199960 rad (%.15f)"), Profiled.Horizon),
        FMath::IsNearlyEqual(Profiled.Horizon, 0.019995978977504523, 1e-13));
    TestTrue(FString::Printf(TEXT("and leaves 0.500256 of the sun (%.12f)"), Profiled.Visible),
        FMath::IsNearlyEqual(Profiled.Visible, 0.5002559862356774, 1e-12));
    TestEqual(TEXT("past it nothing 100 m high could rise over the wall: four steps read"), Profiled.Reads, 4);
    return true;
}

bool FSunShadowExitsTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    const double Radius = 0.03;
    const FVector3d D(1.0, 0.0, 0.0);
    const FVector3d East(0.0, 1.0, 0.0);

    // Smooth ground, 1 cm of relief: the only horizon is the sphere's own.
    const FSmoothGround Smooth(6.0e8, 1.0);
    const FSunVisibility Level = Visible(Smooth, D, LightAt(D, East, 0.0, Radius), 100.0, Smooth.MaxSlope());
    TestTrue(FString::Printf(TEXT("its centre on the level, half the sun shows (%.8f), finite"), Level.Visible),
        FMath::IsFinite(Level.Visible) && FMath::Abs(Level.Visible - 0.5) <= 1.0e-5);
    TestTrue(FString::Printf(TEXT("and the bound ends the march: nothing on smooth ground rises past the first sample (%d reads)"), Level.Reads),
        Level.Reads > 1 && Level.Reads < 1 + DefaultSamples);
    const FSunVisibility Up = Visible(Smooth, D, LightAt(D, East, 2.0 * Radius, Radius), 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("two radii up it is whole, by the day's exit, unread"), Up.Visible == 1.0 && Up.Reads == 0);
    const FSunVisibility Down = Visible(Smooth, D, LightAt(D, East, -2.0 * Radius, Radius), 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("two radii down it is hidden, by the night's, after its own height"), Down.Visible == 0.0 && Down.Reads == 1);
    double Rising = -1.0;
    for (int32 Step = -6; Step <= 6; ++Step)
    {
        const double Seen = Visible(Smooth, D, LightAt(D, East, Step * 0.25 * Radius, Radius), 100.0, Smooth.MaxSlope()).Visible;
        TestTrue(FString::Printf(TEXT("and it rises with the sun (%.2f radii: %.6f)"), Step * 0.25, Seen), Seen >= Rising);
        Rising = Seen;
    }

    // The star overhead and underfoot: no azimuth to march along.
    const FSunLight Zenith{ D, Radius };
    const FSunVisibility Overhead = Visible(Smooth, D, Zenith, 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("the star overhead is whole, finite"), Overhead.Visible == 1.0 && FMath::IsFinite(Overhead.Horizon));
    const FSunLight Nadir{ -D, Radius };
    const FSunVisibility Underfoot = Visible(Smooth, D, Nadir, 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("the star underfoot is hidden, finite"), Underfoot.Visible == 0.0 && FMath::IsFinite(Underfoot.Horizon));

    // A star seen from inside its radius: pi/2, clamped to SunRadiusMax, so
    // a centre 0.1 rad under the level shows DiscAbove(0.2) of it.
    const FSunVisibility Huge = Visible(Smooth, D, LightAt(D, East, -0.1, UE_DOUBLE_HALF_PI), 100.0, Smooth.MaxSlope());
    TestTrue(FString::Printf(TEXT("a pi/2 star 0.1 rad down is clamped to 0.5 rad and shows 0.373530 (%.8f)"), Huge.Visible),
        FMath::Abs(Huge.Visible - 0.373530039052331) <= 1.0e-5);

    // No ground, and no star: nothing is cast and nothing is read.
    const FSmoothGround Sea(6.0e8, 0.0);
    for (const double Degrees : { 2.0, -30.0 })
    {
        const FSunVisibility Wet = Visible(Sea, D, LightAt(D, East, FMath::DegreesToRadians(Degrees), Radius), 100.0, Sea.MaxSlope());
        TestTrue(FString::Printf(TEXT("a world with no ground casts nothing at %.0f degrees"), Degrees), Wet.Visible == 1.0 && Wet.Reads == 0);
    }
    const FSunVisibility Dark = Visible(Smooth, D, FSunLight(), 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("without a star nothing is cast"), Dark.Visible == 1.0 && Dark.Reads == 0);

    // The real ground's exits meet its march with no step: just over the
    // day's the disc is whole unread, just under it the march finds it
    // whole; just under the night's it is hidden unread, just over it the
    // march finds it hidden.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Real(Params);
    const double Steep = SteepestSlope(Params);
    const double Day = FMath::Atan(Steep) + Radius;
    FRandomStream Stream(20260928);
    for (int32 Sample = 0; Sample < 24; ++Sample)
    {
        const FVector3d Here(Stream.GetUnitVector());
        const FVector3d Toward(Stream.GetUnitVector());
        const FSunVisibility Over = Visible(Real, Here, LightAt(Here, Toward, Day + 1.0e-6, Radius), 1.0e4, Steep);
        const FSunVisibility Under = Visible(Real, Here, LightAt(Here, Toward, Day - 0.5 * Radius, Radius), 1.0e4, Steep);
        TestTrue(TEXT("over the day's exit the disc is whole, unread"), Over.Visible == 1.0 && Over.Reads == 0);
        TestTrue(FString::Printf(TEXT("under it the march runs and finds it whole (%.6f, %d reads)"), Under.Visible, Under.Reads),
            Under.Reads > 0 && Under.Visible >= 0.999);
        const double Dip = NightDip(Real.RadiusCm(), Real.Height(Here, 1.0e4), Real.MinHeightCm());
        const FSunVisibility Below = Visible(Real, Here, LightAt(Here, Toward, -Dip - Radius - 1.0e-6, Radius), 1.0e4, Steep);
        const FSunVisibility Above = Visible(Real, Here, LightAt(Here, Toward, -Dip - Radius + 1.0e-6, Radius), 1.0e4, Steep);
        TestTrue(TEXT("under the night's exit the disc is hidden, only the point's height read"), Below.Visible == 0.0 && Below.Reads == 1);
        TestTrue(FString::Printf(TEXT("over it the march runs and finds it hidden (%.6f, %d reads)"), Above.Visible, Above.Reads),
            Above.Reads > 1 && Above.Visible <= 1.0e-3);
    }
    return true;
}

bool FSunShadowScheduleTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // Downhill on the tilted plane nothing hides the sun and nothing ends
    // the march early, so every sample is read: the point's own height at the
    // footprint, then the geometric series from Nearest to MarchEnd, each read
    // at twice the gap ahead of it.
    const double R = 6.0e8;
    const double Footprint = 3.0e3;
    const FVector3d D(1.0, 0.0, 0.0);
    const FVector3d Up(0.0, 1.0, 0.0);
    const FTiltedGround Tilted(R, FMath::Tan(FMath::DegreesToRadians(5.0)), Up);
    const FRecordingGround Recorded(Tilted);
    const FSunLight Sun = LightAt(D, -Up, 0.0, 0.03);
    const FSunVisibility Seen = Visible(Recorded, D, Sun, Footprint, Tilted.MaxSlope(), 12);
    if (!TestEqual(TEXT("the point's own height, then twelve samples"), Recorded.Reads.Num(), 13))
    {
        return false;
    }
    TestEqual(TEXT("and the result counts them"), Seen.Reads, 13);
    TestTrue(TEXT("the first read is the point, at its footprint"), Recorded.Reads[0].D.Equals(D, 1e-15) && Recorded.Reads[0].FootprintCm == Footprint);
    const double End = MarchEnd(R, 0.0, Tilted.MaxHeightCm(), -0.03);
    const double Nearest = Footprint / R;
    const double Growth = FMath::Pow(End / Nearest, 1.0 / 11.0);
    for (int32 Sample = 0; Sample < 12; ++Sample)
    {
        const double Along = Nearest * FMath::Pow(Growth, Sample);
        const FVector3d Want = D * FMath::Cos(Along) - Up * FMath::Sin(Along);
        const double WantFootprint = FMath::Max(Footprint, FootprintFactor * Along * (Growth - 1.0) * R);
        const FRecordingGround::FRead& Read = Recorded.Reads[Sample + 1];
        TestTrue(FString::Printf(TEXT("sample %d lies %.3e rad toward the sun (%.3e off)"), Sample, Along, (Read.D - Want).Size()),
            (Read.D - Want).Size() <= 1e-12);
        TestTrue(FString::Printf(TEXT("and is read at twice the gap ahead, %.6g cm (%.6g)"), WantFootprint, Read.FootprintCm),
            FMath::IsNearlyEqual(Read.FootprintCm, WantFootprint, 1e-9 * WantFootprint));
    }
    return true;
}

bool FSunShadowAgainstProfileTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // The tiles' geometric march against the map's dense one at the same
    // footprint (300 m, a tile's spacing near 30 km up). Planning measured,
    // at 12 samples: the shaded share 0.79-1.03 of the dense one's; mean
    // |dv| 0.04-0.07 at 5 degrees and 0.10-0.14 at 2; at 8 samples 0.07-0.14
    // at 5 -- over this test's bound, so 8 is a quality NO-GO, never a looser
    // bound. Held: the shaded share 0.6-1.2 of the truth's, the penumbra 0.5-2,
    // mean |dv| under 0.10 at 5 degrees and 0.18 at 2.
    FWorldReliefParams Fourth = GroundFixtures::FixtureParams();
    FWorldReliefParams Fifth = GroundFixtures::FixtureParams();
    Fifth.RadiusCm = 0.843852 * 6.3781e8;
    Fifth.PeakCm = 6.12194e5;
    Fifth.Cratering = 0.15;
    struct FCase { const TCHAR* Name; const FWorldReliefParams* Params; double SunRadius; double Degrees; double MeanGap; };
    const FCase Cases[] = {
        { TEXT("IV-like, 5 degrees"), &Fourth, 0.02903, 5.0, 0.10 },
        { TEXT("IV-like, 2 degrees"), &Fourth, 0.02903, 2.0, 0.18 },
        { TEXT("V-like, 5 degrees"), &Fifth, 0.01700, 5.0, 0.10 },
        { TEXT("V-like, 2 degrees"), &Fifth, 0.01700, 2.0, 0.18 },
    };
    constexpr double Footprint = 3.0e4;
    constexpr int32 Count = 100;
    FRandomStream Stream(20260929);
    for (const FCase& Case : Cases)
    {
        int32 MarchShaded = 0;
        int32 TruthShaded = 0;
        int32 MarchPenumbra = 0;
        int32 TruthPenumbra = 0;
        double Gap = 0.0;
        double GapAt[2] = { 0.0, 0.0 };
        for (int32 Sample = 0; Sample < Count; ++Sample)
        {
            FWorldReliefParams Params = *Case.Params;
            Params.SeedOffset = OffsetFrom(Stream);
            const FReliefGround Ground(Params);
            const double Steep = SteepestSlope(Params);
            const FVector3d D(Stream.GetUnitVector());
            const FSunLight Sun = LightAt(D, FVector3d(Stream.GetUnitVector()), FMath::DegreesToRadians(Case.Degrees), Case.SunRadius);
            const double Truth = Dense(Ground, D, Sun, Footprint, Steep).Visible;
            const double March = Visible(Ground, D, Sun, Footprint, Steep).Visible;
            GapAt[0] += FMath::Abs(Visible(Ground, D, Sun, Footprint, Steep, 8).Visible - Truth);
            GapAt[1] += FMath::Abs(Visible(Ground, D, Sun, Footprint, Steep, 16).Visible - Truth);
            MarchShaded += March < 0.5 ? 1 : 0;
            TruthShaded += Truth < 0.5 ? 1 : 0;
            MarchPenumbra += March > 0.01 && March < 0.99 ? 1 : 0;
            TruthPenumbra += Truth > 0.01 && Truth < 0.99 ? 1 : 0;
            Gap += FMath::Abs(March - Truth);
        }
        const double Ratio = TruthShaded > 0 ? static_cast<double>(MarchShaded) / TruthShaded : 0.0;
        const double PenumbraRatio = TruthPenumbra > 0 ? static_cast<double>(MarchPenumbra) / TruthPenumbra : 0.0;
        AddInfo(FString::Printf(TEXT("%s: shaded %d against the dense march's %d (%.2f), penumbra %d against %d, mean |dv| %.3f at %d samples, %.3f at 8, %.3f at 16"),
            Case.Name, MarchShaded, TruthShaded, Ratio, MarchPenumbra, TruthPenumbra, Gap / Count, DefaultSamples, GapAt[0] / Count, GapAt[1] / Count));
        TestTrue(FString::Printf(TEXT("%s: the march shades 0.6 to 1.2 of the dense march's ground (%.2f of %d)"), Case.Name, Ratio, TruthShaded),
            TruthShaded >= 10 && Ratio >= 0.6 && Ratio <= 1.2);
        TestTrue(FString::Printf(TEXT("%s: its penumbra is the sun's, 0.5 to 2 of the dense one's (%.2f)"), Case.Name, PenumbraRatio),
            PenumbraRatio >= 0.5 && PenumbraRatio <= 2.0);
        TestTrue(FString::Printf(TEXT("%s: mean |dv| under %.2f (%.3f)"), Case.Name, Case.MeanGap, Gap / Count), Gap / Count <= Case.MeanGap);
    }
    return true;
}

bool FSunShadowSteepestSlopeTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // The day exit's slopes, re-measured: 128 worlds' offsets and 1,024
    // directions each, from FRandomStream(20260928), of |grad| along the
    // ground at footprint 0 -- the detail bands on a world with twenty of
    // them (8e9 cm: a world needs 6.3e9 cm of radius for a twentieth band and
    // 1.26e10 for a twenty-first, and no rocky world is within a factor of five
    // of either), and all six crater bands. Planning measured 16.72 and 2.30
    // with another generator. The constants times the margin must be 1.3 to
    // 1.8 times what this run measures: steeper than anything sampled, and
    // not so steep the day side stops exiting.
    FWorldReliefParams Big;
    Big.RadiusCm = 8.0e9;
    Big.PeakCm = 1.0e5;
    Big.Cratering = 1.0;
    Big.Ground = EGround::Solid;
    TestEqual(TEXT("the sampled world carries twenty detail bands"), FWorldRelief(Big).GetDetailFrequencies().Num(), 20);
    FRandomStream Stream(20260928);
    double Detail = 0.0;
    double Craters = 0.0;
    for (int32 World = 0; World < 128; ++World)
    {
        Big.SeedOffset = OffsetFrom(Stream);
        const FWorldRelief Relief(Big);
        for (int32 Sample = 0; Sample < 1024; ++Sample)
        {
            const FVector3d D(Stream.GetUnitVector());
            FVector3d G;
            Relief.DetailSum(D, 0.0, &G);
            Detail = FMath::Max(Detail, (G - D * FVector3d::DotProduct(G, D)).Size());
            Relief.CraterSum(D, 0.0, &G);
            Craters = FMath::Max(Craters, (G - D * FVector3d::DotProduct(G, D)).Size());
        }
    }
    AddInfo(FString::Printf(TEXT("steepest |grad S| along the ground: detail %.3f, craters %.3f"), Detail, Craters));
    TestTrue(FString::Printf(TEXT("the detail's margin, %.2f, is 1.3 to 1.8 times the steepest sampled (%.3f)"), SteepestMargin * DetailGradientSampled, Detail),
        SteepestMargin * DetailGradientSampled >= 1.3 * Detail && SteepestMargin * DetailGradientSampled <= 1.8 * Detail);
    TestTrue(FString::Printf(TEXT("the craters', %.2f, likewise (%.3f)"), SteepestMargin * CraterGradientSampled, Craters),
        SteepestMargin * CraterGradientSampled >= 1.3 * Craters && SteepestMargin * CraterGradientSampled <= 1.8 * Craters);
    // And the real ground's slope never passes it: the fixture's steepest
    // finite difference between two footprints' heights.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Real(Params);
    double Read = 0.0;
    for (int32 Sample = 0; Sample < 2048; ++Sample)
    {
        const FVector3d D(Stream.GetUnitVector());
        const FVector3d Along = FVector3d(Stream.GetUnitVector()).Cross(D).GetSafeNormal();
        constexpr double Gap = 1.0e4;
        const FVector3d There = (D + Along * (Gap / Real.RadiusCm())).GetSafeNormal();
        Read = FMath::Max(Read, FMath::Abs(Real.Height(There, 2.0 * Gap) - Real.Height(D, Gap)) / Gap);
    }
    TestTrue(FString::Printf(TEXT("the fixture's steepest read slope (%.4f) is under the exit's (%.4f)"), Read, SteepestSlope(Params)), Read < SteepestSlope(Params));
    return true;
}

#endif
```

- [ ] **Step 2: Run them to see them fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails, because `Surface/SunShadow.h` does not exist.

- [ ] **Step 3: The header.** Create `Source/DeepSpace/Surface/SunShadow.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Surface/WorldReliefParams.h"

class IGroundField;

/**
 * The cast shadow (the developer's ruling on slice (b)'s build, 2026-09-28:
 * baked, not marched): how much of its star a point of the ground sees past
 * the ground toward it. Pure: no UObject, no world, no CVar.
 *
 * Worlds do not spin and nothing orbits in this slice, so the star stands
 * still over every surface and the shadow is a fixed function of where the
 * point is. It is computed where the ground is built -- a tile's vertices,
 * off the game thread (TerrainTile::Build) -- and once a world for the
 * orbit (SunShadowMap), and never per pixel.
 *
 * The horizon toward the star is the highest elevation, above the point's
 * own level, of the ground along the great circle toward the star's
 * azimuth, from the exact triangle of the point, the ground and the world's
 * centre. The star is a disc of its own angular radius, and what shows is
 * the share of the disc above that horizon: DiscAbove((horizon - elevation)
 * / radius). A shadow is as dark as the night side: there is no fill light.
 *
 * Two exits skip the march. The night's is a proof: no horizon is lower
 * than the dip to the lowest ground the world can have. The day's is a
 * measurement with a margin: above atan(SteepestSlope) plus the star's
 * radius nothing can rise over the disc (SteepestSlope's comment). Inside
 * the march two exits are exact: the disc already hidden, and no ground from
 * there on -- none above the peak, all of it curving away -- able to rise
 * over what is found.
 */
namespace SunShadow
{
    /** The star as a surface sees it: the unit direction from the body's
     *  centre to the star, in the body's axes (the universe's: worlds do not
     *  spin) -- the sky's own light, SkyProjection::LightDirection -- and its
     *  angular radius, rad. A zero direction is no star, and casts nothing. */
    struct FSunLight
    {
        FVector3d Direction = FVector3d::ZeroVector;
        double AngularRadius = 0.0;

        bool IsSet() const { return !Direction.IsNearlyZero(); }
    };

    struct FSunVisibility
    {
        /** The star's disc seen past the ground, 0..1. */
        double Visible = 1.0;

        /** The highest horizon found, rad above the level; -pi/2 where none
         *  was read. */
        double Horizon = -UE_DOUBLE_HALF_PI;

        /** Heights read: the cost. */
        int32 Reads = 0;
    };

    /** A star seen from inside its own radius is pi/2 in radius, where the
     *  share of a disc stops meaning anything: clamped here. */
    inline constexpr double SunRadiusMax = 0.5;

    /** Nothing in the ground is finer than FWorldRelief::BandLimitCm (5 m),
     *  so the first sample is never nearer than half of it. */
    inline constexpr double NearestCm = 250.0;

    /** Each sample reads the heights at this many times the gap ahead of it:
     *  the face's own fade (filter_pixels), so no band is read at under two
     *  samples a cycle along the march. */
    inline constexpr double FootprintFactor = 2.0;

    /** How many samples a tile's vertex marches (ds.Terrain.ShadowSamples):
     *  planning measured 12 at mean |dv| 0.04-0.07 from the dense march at 5
     *  degrees, and 8 at 0.07-0.14 (DeepSpace.Surface.SunShadow.AgainstProfile). */
    inline constexpr int32 DefaultSamples = 12;

    /** The steepest |grad S| along the ground of twenty detail bands, and of
     *  all six crater bands, at footprint 0, over 131,072 samples (the plan's
     *  harness; DeepSpace.Surface.SunShadow.SteepestSlope re-measures it). A
     *  sample, not a proof: FWorldRelief::MaxSlope is the proof, and it is
     *  six times this, which would leave no day side to exit. */
    inline constexpr double DetailGradientSampled = 16.72;
    inline constexpr double CraterGradientSampled = 2.30;
    inline constexpr double SteepestMargin = 1.5;

    /** The share of a disc above a straight horizon X of its radii above
     *  the disc's centre: 1 at X <= -1, 0.5 at 0, 0 at X >= 1. */
    DEEPSPACE_API double DiscAbove(double X);

    /** The elevation, rad above the level at the point, of ground ThereCm
     *  above the datum at arc Arc (rad) along a great circle, seen from
     *  HereCm above it, over a datum of RadiusCm. Exact. */
    DEEPSPACE_API double Elevation(double RadiusCm, double HereCm, double ThereCm, double Arc);

    /** How far along the ground (rad) the march must look: where ground at
     *  PeakCm, the highest the world can have, falls under Lower (rad above
     *  the level) as the sphere curves away. acos(rho cos Lower) - Lower,
     *  rho = (R + Here) / (R + Peak); 0 where nothing can rise over Lower. */
    DEEPSPACE_API double MarchEnd(double RadiusCm, double HereCm, double PeakCm, double Lower);

    /** How far below the level (rad) the lowest ground the world can have,
     *  LowestCm, is at its tangent from HereCm. No horizon is lower, so a
     *  star whose upper edge is under -NightDip is hidden: a proof. */
    DEEPSPACE_API double NightDip(double RadiusCm, double HereCm, double LowestCm);

    /**
     * The shadow at D, a tile vertex's: DefaultSamples geometric samples from
     * Nearest (the footprint, or NearestCm) to MarchEnd, each read at
     * FootprintFactor times the gap ahead of it. SteepestSlope is the
     * day exit's, rise over run: SteepestSlope(Params) below for the real
     * ground, Ground.MaxSlope() for a proof. HereCm, if given, is D's own
     * height at the footprint, already read.
     */
    DEEPSPACE_API FSunVisibility Visible(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm,
                                         double SteepestSlope, int32 Samples = DefaultSamples, TOptional<double> HereCm = {});

    /**
     * The same horizon over a profile already read, the orbit map's: Heights[0]
     * is the point's own, Heights[k] the ground at arc k x Step (rad) toward the
     * star, all at one footprint; SunElevation the star's centre above the
     * level. Every sample out to MarchEnd is one, so nothing lies between
     * them and nothing is read twice. Reads counts the profile entries
     * past the point that were read.
     */
    DEEPSPACE_API FSunVisibility AlongProfile(double RadiusCm, double PeakCm, double LowestCm, double SteepestSlope,
                                              TConstArrayView<double> Heights, double Step, double SunElevation, double SunRadius);

    /** The real ground's steepest slope, rise over run, for the day exit:
     *  SlopeScale x SteepestMargin x (DetailGradientSampled + Cratering x
     *  CraterGradientSampled). PeakCap's slope is at most 1, so the capped
     *  height is never steeper than its sum. 0 on a world with no ground. */
    DEEPSPACE_API double SteepestSlope(const FWorldReliefParams& Params);
}
```

- [ ] **Step 4: The march.** Create `Source/DeepSpace/Surface/SunShadow.cpp`:

```cpp
#include "Surface/SunShadow.h"

#include "Surface/GroundField.h"
#include "Surface/WorldRelief.h"

double SunShadow::DiscAbove(double X)
{
    const double C = FMath::Clamp(X, -1.0, 1.0);
    return (FMath::Acos(C) - C * FMath::Sqrt(FMath::Max(0.0, 1.0 - C * C))) / UE_DOUBLE_PI;
}

double SunShadow::Elevation(double RadiusCm, double HereCm, double ThereCm, double Arc)
{
    // (R + There) cos a - (R + Here), without cancelling two numbers near R:
    // (There - Here) - 2 (R + There) sin^2(a / 2).
    const double Half = FMath::Sin(0.5 * Arc);
    return FMath::Atan2((ThereCm - HereCm) - 2.0 * (RadiusCm + ThereCm) * Half * Half, (RadiusCm + ThereCm) * FMath::Sin(Arc));
}

double SunShadow::MarchEnd(double RadiusCm, double HereCm, double PeakCm, double Lower)
{
    if (Lower >= UE_DOUBLE_HALF_PI)
    {
        return 0.0;
    }
    const double Rho = (RadiusCm + HereCm) / (RadiusCm + PeakCm);
    return FMath::Max(0.0, FMath::Acos(FMath::Clamp(Rho * FMath::Cos(Lower), -1.0, 1.0)) - Lower);
}

double SunShadow::NightDip(double RadiusCm, double HereCm, double LowestCm)
{
    return HereCm > LowestCm ? FMath::Acos(FMath::Clamp((RadiusCm + LowestCm) / (RadiusCm + HereCm), -1.0, 1.0)) : 0.0;
}

SunShadow::FSunVisibility SunShadow::Visible(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm,
                                             double SteepestSlope, int32 Samples, TOptional<double> HereCm)
{
    FSunVisibility Out;
    const double R = Ground.RadiusCm();
    const double Peak = Ground.MaxHeightCm();
    if (!Sun.IsSet() || !(R > 0.0) || !(Peak > Ground.MinHeightCm()))
    {
        // No star, or no ground to cast with: an ocean's, a giant's.
        return Out;
    }
    const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunRadiusMax);
    const FVector3d L = Sun.Direction.GetSafeNormal();
    const double SinT = FVector3d::DotProduct(L, D);
    const FVector3d Level = L - D * SinT;
    const double CosT = Level.Size();
    const double Theta = FMath::Atan2(SinT, CosT);
    // The day: over the steepest ground there is, the disc is whole.
    const double DayExit = FMath::Atan(SteepestSlope) + Radius;
    if (Theta >= DayExit)
    {
        return Out;
    }
    const double Here = HereCm.IsSet() ? HereCm.GetValue() : Ground.Height(D, FootprintCm);
    Out.Reads = HereCm.IsSet() ? 0 : 1;
    // The night: under every horizon the ground can make, the disc is hidden.
    if (Theta + Radius <= -NightDip(R, Here, Ground.MinHeightCm()))
    {
        Out.Visible = 0.0;
        return Out;
    }
    if (CosT < 1.0e-12)
    {
        // The star straight up, under a steepest slope past vertical: no
        // azimuth, and nothing above.
        return Out;
    }
    const FVector3d Toward = Level / CosT;
    const double End = MarchEnd(R, Here, Peak, Theta - Radius);
    const double Nearest = FMath::Max(FootprintCm, NearestCm) / R;
    if (!(End > Nearest))
    {
        return Out;
    }
    const int32 Count = FMath::Max(Samples, 2);
    const double Growth = FMath::Pow(End / Nearest, 1.0 / (Count - 1));
    double Along = Nearest;
    for (int32 Sample = 0; Sample < Count; ++Sample)
    {
        // No ground from here on -- none above the peak, all of it curving
        // away -- can rise over what is already found.
        if (Elevation(R, Here, Peak, Along) <= Out.Horizon)
        {
            break;
        }
        const double SeenCm = FMath::Max(FootprintCm, FootprintFactor * Along * (Growth - 1.0) * R);
        const FVector3d There = D * FMath::Cos(Along) + Toward * FMath::Sin(Along);
        Out.Horizon = FMath::Max(Out.Horizon, Elevation(R, Here, Ground.Height(There, SeenCm), Along));
        ++Out.Reads;
        // The disc is already hidden.
        if (Out.Horizon >= Theta + Radius)
        {
            break;
        }
        Along *= Growth;
    }
    Out.Visible = DiscAbove((Out.Horizon - Theta) / Radius);
    return Out;
}

SunShadow::FSunVisibility SunShadow::AlongProfile(double RadiusCm, double PeakCm, double LowestCm, double SteepestSlope,
                                                  TConstArrayView<double> Heights, double Step, double SunElevation, double SunRadius)
{
    FSunVisibility Out;
    if (Heights.Num() == 0 || !(RadiusCm > 0.0) || !(PeakCm > LowestCm) || !(Step > 0.0))
    {
        return Out;
    }
    const double Radius = FMath::Clamp(SunRadius, 1.0e-6, SunRadiusMax);
    const double Theta = SunElevation;
    if (Theta - Radius >= FMath::Atan(SteepestSlope))
    {
        return Out;
    }
    const double Here = Heights[0];
    if (Theta + Radius <= -NightDip(RadiusCm, Here, LowestCm))
    {
        Out.Visible = 0.0;
        return Out;
    }
    const double End = MarchEnd(RadiusCm, Here, PeakCm, Theta - Radius);
    for (int32 K = 1; K < Heights.Num() && K * Step <= End; ++K)
    {
        const double Arc = K * Step;
        if (Elevation(RadiusCm, Here, PeakCm, Arc) <= Out.Horizon)
        {
            break;
        }
        Out.Horizon = FMath::Max(Out.Horizon, Elevation(RadiusCm, Here, Heights[K], Arc));
        ++Out.Reads;
        if (Out.Horizon >= Theta + Radius)
        {
            break;
        }
    }
    Out.Visible = DiscAbove((Out.Horizon - Theta) / Radius);
    return Out;
}

double SunShadow::SteepestSlope(const FWorldReliefParams& Params)
{
    return FWorldRelief(Params).SlopeScale() * SteepestMargin
        * (DetailGradientSampled + FMath::Max(Params.Cratering, 0.0) * CraterGradientSampled);
}
```

- [ ] **Step 5: Run them to see them pass.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.SunShadow`

Expected: all five PASS. Copy these into the task report:
- `.AgainstProfile`'s four info lines: the ratios, and mean |dv| at 12, 8 and 16 samples.
- `.SteepestSlope`'s measured slopes.

A ratio under 0.6, or a 5-degree mean |dv| over 0.10 at 12 samples, means the march is not what planning measured. Stop and report the numbers, and do not loosen the bound. A measured detail slope outside 13.9-19.3 (and so a margin outside 1.3-1.8) means the constant's claim is off: stop and report it, and do not re-tune it without the rule.

- [ ] **Step 6: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Surface/SunShadow.h Source/DeepSpace/Surface/SunShadow.cpp Source/DeepSpace/Tests/SunShadowTest.cpp && \
git commit -qm "feat(surface): SunShadow -- the cast shadow's horizon march, pure, for the bake

The highest horizon toward the star along the great circle, by the exact
triangle; the star's own disc above it; the night's exit a proof (the dip to
the lowest ground), the day's a sampled steepest slope with a 1.5 margin;
geometric samples for a tile's vertices (Visible), every texel ahead for the
orbit's map (AlongProfile). DeepSpace.Surface.SunShadow.KnownValues, .Exits,
.Schedule, .AgainstProfile, .SteepestSlope.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Prove them.** Run each mutation after the commit, then `./build.sh`:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp 'return (FMath::Acos(C) - C * FMath::Sqrt' 'return (FMath::Acos(C) + C * FMath::Sqrt' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp '(ThereCm - HereCm) - 2.0 * (RadiusCm + ThereCm)' '(ThereCm - HereCm) - (RadiusCm + ThereCm)' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp 'Rho * FMath::Cos(Lower), -1.0, 1.0)) - Lower)' 'Rho * FMath::Cos(Lower), -1.0, 1.0)) + Lower)' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp '(RadiusCm + LowestCm) / (RadiusCm + HereCm)' '(RadiusCm + HereCm) / (RadiusCm + LowestCm)' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp 'D * FMath::Cos(Along) + Toward * FMath::Sin(Along)' 'D * FMath::Cos(Along) - Toward * FMath::Sin(Along)' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp 'const double DayExit = FMath::Atan(SteepestSlope) + Radius;' 'const double DayExit = FMath::Atan(SteepestSlope);' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp 'const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunRadiusMax);' 'const double Radius = Sun.AngularRadius;' 'DeepSpace.Surface.SunShadow.Exits$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp 'FMath::Max(FootprintCm, FootprintFactor * Along * (Growth - 1.0) * R)' 'FMath::Max(FootprintCm, Along * (Growth - 1.0) * R)' 'DeepSpace.Surface.SunShadow.Schedule$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp '        if (Elevation(R, Here, Peak, Along) <= Out.Horizon)' '        if (false && Elevation(R, Here, Peak, Along) <= Out.Horizon)' 'DeepSpace.Surface.SunShadow.Exits$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.cpp '        const double Arc = K * Step;' '        const double Arc = (K + 1) * Step;' 'DeepSpace.Surface.SunShadow.KnownValues$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadow.h 'inline constexpr double SteepestMargin = 1.5;' 'inline constexpr double SteepestMargin = 1.0;' 'DeepSpace.Surface.SunShadow.SteepestSlope$'; \
./build.sh
```

Expected: eleven `KILLED`. What kills each:
- The `+` disc is symmetric and falling and agrees at -1, 0 and 1. It is killed by the known value at 0.5 (0.4712 against 0.195501).
- The halved curvature misses the flat sphere's -a/2.
- The `+ Lower` end misses 0.1 and 0.161766.
- The inverted dip is an arccos over 1, clamped to 0, against 0.0632245.
- The march that turns away from the sun reads the plane downhill: the uphill sun is whole, not 0.8045.
- The day exit without `r` returns whole at half a radius under the slope's edge.
- The unclamped radius shows DiscAbove(0.064), 0.459, for the pi/2 sun.
- The footprint read at once the gap misses the schedule.
- The march without its bound reads all 13 heights on smooth ground.
- The profile shifted a step reads the wall at 4 km.
- The margin at 1.0 falls under 1.3 times the measured slope.

The two tests' own per-sample checks are proven by these mutants too, since each is killed by an exact value, not by a trend.

---

## Task 3: the sky's light, one function

**Owner:** T. **Depends on:** Task 2 (`SunShadow::FSunLight`).

**Files:**
- Modify: `Source/DeepSpace/Sky/SkyProjection.h`, `Source/DeepSpace/Sky/SkyProjection.cpp`
- Create: `Source/DeepSpace/Tests/SunLightTest.cpp` (`DeepSpace.Sky.SunLightIsTheSkys`)

**Interfaces:**
- Consumes: `FSkySystem`, `FSkyBody`, `SkyProjection::Project`, `SkyTestFixtures::System`, `SkyTestFixtures::Opening`.
- Produces: `FVector SkyProjection::LightDirection(const FSkyBody& Body, const FSkyBody& Star)`, `double SkyProjection::StarAngularRadius(const FSkyBody& Body, const FSkyBody& Star)`, `SunShadow::FSunLight SkyProjection::SunLightOf(const FSkySystem& System, int32 Body)`. `Project` writes `FSkyBodyView::LightDirection` through `LightDirection`, so the shadow's light and the shading's are one function.

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/SunLightTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Sky/SkyProjection.h"
#include "Surface/SunShadow.h"
#include "Tests/SkyTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunLightIsTheSkysTest, "DeepSpace.Sky.SunLightIsTheSkys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The cast shadow is cast by the light the sky shades with (the cast-shadow
 * plan's constraint): SunLightOf's direction is Project's LightDirection bit
 * for bit -- the body's centre toward its star, universe axes -- and its
 * radius the star's, asin(R_star / d). Worked out here from the fixture's
 * positions too, so a mutant moving both at once is still caught.
 */
bool FSunLightIsTheSkysTest::RunTest(const FString& Parameters)
{
    const FSkySystem System = SkyTestFixtures::System();
    const FSkyFrame Frame = SkyProjection::Project(System, SkyTestFixtures::Opening(), FSkyViewParams());
    const FSkyBody& Star = System.Bodies[SkyTestFixtures::StarIndex];
    int32 Lit = 0;
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = System.Bodies[Index];
        const SunShadow::FSunLight Sun = SkyProjection::SunLightOf(System, Index);
        if (Body.Kind == ESkyBodyKind::Star)
        {
            TestFalse(TEXT("the star casts on nothing of its own"), Sun.IsSet());
            continue;
        }
        ++Lit;
        const FVector3d Want = FVector3d(Star.Position - Body.Position).GetSafeNormal();
        TestTrue(FString::Printf(TEXT("%s: the shadow's light is the sky's, bit for bit"), *Body.Id.ToString()),
            Sun.Direction == FVector3d(Frame.Bodies[Index].LightDirection));
        TestTrue(FString::Printf(TEXT("%s: from the body's centre toward its star"), *Body.Id.ToString()),
            FVector3d::DotProduct(Sun.Direction, Want) > 1.0 - 1e-12);
        const double Distance = FVector3d(Star.Position - Body.Position).Size();
        TestTrue(FString::Printf(TEXT("%s: the star's own radius, asin(R / d) (%.9f)"), *Body.Id.ToString(), Sun.AngularRadius),
            FMath::IsNearlyEqual(Sun.AngularRadius, FMath::Asin(Star.Radius / Distance), 1e-15));
    }
    TestTrue(TEXT("the fixture has lit bodies"), Lit > 0);
    TestFalse(TEXT("an index the system does not have casts nothing"), SkyProjection::SunLightOf(System, 99).IsSet());
    return true;
}

#endif
```

- [ ] **Step 2: Run it to see it fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails, because `SkyProjection::SunLightOf` is undeclared.

- [ ] **Step 3: The functions.** In `Source/DeepSpace/Sky/SkyProjection.h`, add `#include "Surface/SunShadow.h"` after the existing includes. In `namespace SkyProjection`, after `Project`, add:

```cpp
    /** The one light a world is lit and shadowed by: the unit direction from
     *  its centre to its star, universe axes -- FSkyBodyView::LightDirection,
     *  and the cast shadow's (SunShadow::FSunLight). Zero if they coincide. */
    DEEPSPACE_API FVector LightDirection(const FSkyBody& Body, const FSkyBody& Star);

    /** The star's angular radius from the body's centre, rad: asin(R / d),
     *  or pi/2 from inside it. */
    DEEPSPACE_API double StarAngularRadius(const FSkyBody& Body, const FSkyBody& Star);

    /** The light the cast shadow of body Body is baked under: its star's
     *  LightDirection and StarAngularRadius. Unset for a star, for an index
     *  the system does not have, and in a system with no star. */
    DEEPSPACE_API SunShadow::FSunLight SunLightOf(const FSkySystem& System, int32 Body);
```

In `Source/DeepSpace/Sky/SkyProjection.cpp`, in `Project`'s planet branch, replace

```cpp
            const FVector ToStar = Star->Position - Body.Position;
            const double StarDistance = ToStar.Size();
            View.LightDirection = StarDistance > 0.0 ? ToStar / StarDistance : FVector::ZeroVector;
```

with

```cpp
            const double StarDistance = (Star->Position - Body.Position).Size();
            View.LightDirection = LightDirection(Body, *Star);
```

At the end of the file, add:

```cpp
FVector SkyProjection::LightDirection(const FSkyBody& Body, const FSkyBody& Star)
{
    const FVector ToStar = Star.Position - Body.Position;
    const double Distance = ToStar.Size();
    return Distance > 0.0 ? ToStar / Distance : FVector::ZeroVector;
}

double SkyProjection::StarAngularRadius(const FSkyBody& Body, const FSkyBody& Star)
{
    const double Distance = (Star.Position - Body.Position).Size();
    return Distance > Star.Radius ? FMath::Asin(Star.Radius / Distance) : 0.5 * UE_DOUBLE_PI;
}

SunShadow::FSunLight SkyProjection::SunLightOf(const FSkySystem& System, int32 Body)
{
    SunShadow::FSunLight Sun;
    const int32 Star = FindStar(System);
    if (Star == INDEX_NONE || !System.Bodies.IsValidIndex(Body) || System.Bodies[Body].Kind == ESkyBodyKind::Star)
    {
        return Sun;
    }
    Sun.Direction = LightDirection(System.Bodies[Body], System.Bodies[Star]);
    Sun.AngularRadius = StarAngularRadius(System.Bodies[Body], System.Bodies[Star]);
    return Sun;
}
```

(`FindStar` is the file's own, in its anonymous namespace, above `Project`.)

- [ ] **Step 4: Run it to see it pass, and the sky's tests beside it.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Sky.SunLightIsTheSkys && ./test.sh DeepSpace.Sky`

Expected: PASS, and every `DeepSpace.Sky.*` test still passes. `Project` computes the same light through the one function.

- [ ] **Step 5: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Sky/SkyProjection.h Source/DeepSpace/Sky/SkyProjection.cpp Source/DeepSpace/Tests/SunLightTest.cpp && \
git commit -qm "feat(sky): the cast shadow's light is the sky's -- SkyProjection::LightDirection, StarAngularRadius, SunLightOf

Project writes FSkyBodyView::LightDirection through the same function the
tiles and the maps bake under. DeepSpace.Sky.SunLightIsTheSkys.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Prove it.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Sky/SkyProjection.cpp 'return Distance > 0.0 ? ToStar / Distance : FVector::ZeroVector;' 'return Distance > 0.0 ? -ToStar / Distance : FVector::ZeroVector;' 'DeepSpace.Sky.SunLightIsTheSkys$'; \
Tools/mutate.sh Source/DeepSpace/Sky/SkyProjection.cpp '    Sun.AngularRadius = StarAngularRadius(System.Bodies[Body], System.Bodies[Star]);' '    Sun.AngularRadius = 2.0 * StarAngularRadius(System.Bodies[Body], System.Bodies[Star]);' 'DeepSpace.Sky.SunLightIsTheSkys$'; \
./build.sh
```

Expected: two `KILLED`. The flipped light moves Project and SunLightOf together, and is caught by the direction worked out from the positions. The doubled radius misses asin(R / d).

---

## Task 4: the tiles carry the shadow

**Owner:** T. **Depends on:** Tasks 2 and 3.

**Files:**
- Modify: `Source/DeepSpace/Surface/TerrainTile.h`, `Source/DeepSpace/Surface/TerrainTile.cpp`. Add `FTileBuild::SunVisible` and `ShadowSeconds`, and `TerrainTile::FTileShadow`, `Build(..., Shadow, Cancel)` and `UV0Of`.
- Modify: `Source/DeepSpace/Surface/TerrainTileComponent.cpp`: UV0 carries the shadow.
- Modify: `Source/DeepSpace/Surface/WorldGround.h`, `Source/DeepSpace/Surface/WorldGround.cpp`. Add:
  - the light each tile is built under, `ds.Terrain.Shadows` and `ds.Terrain.ShadowSamples`, the rebuild when any of them changes, and a line in `Describe`;
  - builds that are cancelled and detached, never waited on, in `Release` and `EndPlay`, counted against the cap until they finish (`Draining`);
  - `GetBuildingCount`, `GetDrainingCount` and `GetTileShadowBytes`, the last measured from the arrays, both copies.
- Modify: `Source/DeepSpace/Surface/WorldReliefParams.h`: `SameRelief`, moved here from `WorldGround.cpp`'s anonymous namespace, so the sky keys its maps by the same test (Task 6).
- Modify: `Source/DeepSpace/Tests/SkyTestWorld.h`. Add `EShadows`: test worlds build without tile shadows or maps unless asked, and each is asked for on its own.
- Create: `Source/DeepSpace/Tests/TileSunShadowTest.cpp` (`DeepSpace.Surface.TileSunShadow`, `DeepSpace.Surface.GroundShadowLight`)

**Interfaces:**
- Consumes: `SunShadow::{FSunLight, Visible, SteepestSlope, DefaultSamples}` (Task 2); `SkyProjection::SunLightOf` (Task 3); `TerrainQuadtree::{KeyAt, CentreDirection, Face}`; `ShipGround::FromRelief`; `GroundFixtures::FixtureParams`; `SkyTestWorld::{FSkyWorld, FScopedCVar}`; `LocalSystem::Here`.
- Produces:

```cpp
struct FTileBuild { /* ... */ TArray<float> SunVisible; double ShadowSeconds; };
namespace TerrainTile
{
    struct FTileShadow { SunShadow::FSunLight Sun; double SteepestSlope = 0.0; int32 Samples = SunShadow::DefaultSamples; };
    FTileBuild Build(const IGroundField& Ground, const FTileKey& Key, const FTileShadow& Shadow = FTileShadow(),
                     const std::atomic<bool>* Cancel = nullptr);
    FVector2f UV0Of(const FTileBuild& Tile, int32 Vertex);
}
const TerrainTile::FTileShadow& AWorldGround::GetTileShadow() const;
int32 AWorldGround::GetBuildingCount() const;
int32 AWorldGround::GetDrainingCount() const;
int64 AWorldGround::GetTileShadowBytes() const;
bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B);   // WorldReliefParams.h, inline
namespace SkyTestWorld { enum class EShadows : uint8 { Off = 0, Tiles = 1, Maps = 2, On = 3 }; }
SkyTestWorld::FSkyWorld::FSkyWorld(const TCHAR* Name, int32 DistantStarCount = 8, EShadows Shadows = EShadows::Off);
```

The CVars are read by name in tests as `ds.Terrain.Shadows` and `ds.Terrain.ShadowSamples`. `ds.Sky.ShadowMaps` and `ds.Sky.ShadowMapWidth` are Task 6's CVars. `FSkyWorld` sets `ds.Sky.ShadowMaps` only if it exists, so this task does not wait on Task 6. `EShadows::Tiles` sets `ds.Terrain.Shadows` 1 and `ds.Sky.ShadowMaps` 0, and `Maps` does the reverse. `On` sets both, and `Off` neither.

- [ ] **Step 1: Write the failing tests.** Create `Source/DeepSpace/Tests/TileSunShadowTest.cpp`:

```cpp
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/TerrainQuadtree.h"
#include "Surface/TerrainTile.h"
#include "Surface/WorldGround.h"
#include "Tests/GroundFixtures.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTileSunShadowTest, "DeepSpace.Surface.TileSunShadow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGroundShadowLightTest, "DeepSpace.Surface.GroundShadowLight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace TileSunShadowLocal
{
    /** The star Elevation rad above D's level, toward Toward's part along it. */
    SunShadow::FSunLight LightAt(const FVector3d& D, const FVector3d& Toward, double Elevation, double AngularRadius)
    {
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        SunShadow::FSunLight Sun;
        Sun.Direction = (D * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
        Sun.AngularRadius = AngularRadius;
        return Sun;
    }
}

/**
 * A tile's vertices carry the cast shadow (the ruling: computed while the
 * tile is built, off the game thread, from the height function): every grid
 * vertex's SunVisible is SunShadow::Visible at its own direction and height,
 * at the tile's spacing -- to the float it is stored in -- the skirt's
 * vertices carry their grid vertex's, and UV0.x is what reaches the mesh.
 * The shadow never moves the ground. Without a light every vertex is whole.
 * A same-level neighbour's shadows on the shared edge are the tile's to the
 * bit; a 2:1 edge's step is measured and printed, not bounded (the shade can
 * step there, where the skirts close only the geometry's crack).
 * The tile is Baemsekai IV-like at level 6 (4.4 km vertices, 141 km across)
 * under a 2-degree sun, where planning measured half the ground shaded: a
 * tile that fixture changes leave uniform is moved, never the bound.
 */
bool FTileSunShadowTest::RunTest(const FString& Parameters)
{
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FGroundFieldRef Field = ShipGround::FromRelief(Params);
    const FTileKey Key = TerrainQuadtree::KeyAt(FVector3d(0.6, -0.48, 0.64).GetSafeNormal(), 6);
    const FVector3d Centre = TerrainQuadtree::CentreDirection(Key);
    TerrainTile::FTileShadow Shadow;
    Shadow.Sun = TileSunShadowLocal::LightAt(Centre, TerrainQuadtree::Face(Key.Face).U, FMath::DegreesToRadians(2.0), 0.029);
    Shadow.SteepestSlope = SunShadow::SteepestSlope(Params);
    const FTileBuild Lit = TerrainTile::Build(*Field, Key);
    const FTileBuild Cast = TerrainTile::Build(*Field, Key, Shadow);

    // The shadow never moves the ground.
    TestTrue(TEXT("the same positions"), Lit.Positions == Cast.Positions);
    TestTrue(TEXT("the same normals"), Lit.Normals == Cast.Normals);
    TestTrue(TEXT("the same heights"), Lit.Heights == Cast.Heights);

    // Every grid vertex is SunShadow::Visible at its own height and the tile's spacing.
    if (!TestEqual(TEXT("one shadow a grid vertex"), Cast.SunVisible.Num(), TerrainTile::GridVerts))
    {
        return false;
    }
    int32 Wrong = 0;
    int32 Shaded = 0;
    int32 Whole = 0;
    for (int32 V = 0; V < TerrainTile::GridVerts; ++V)
    {
        const float Want = static_cast<float>(SunShadow::Visible(*Field, Cast.Directions[V], Shadow.Sun, Cast.SpacingCm,
            Shadow.SteepestSlope, Shadow.Samples, Cast.Heights[V]).Visible);
        Wrong += Cast.SunVisible[V] == Want ? 0 : 1;
        Shaded += Cast.SunVisible[V] < 0.5f ? 1 : 0;
        Whole += Cast.SunVisible[V] > 0.5f ? 1 : 0;
        TestEqual(TEXT("UV0.x is the vertex's shadow"), TerrainTile::UV0Of(Cast, V).X, Cast.SunVisible[V]);
    }
    TestEqual(TEXT("every grid vertex carries SunShadow::Visible at its height and the tile's spacing"), Wrong, 0);
    AddInfo(FString::Printf(TEXT("level 6 under a 2-degree sun: %d of %d vertices under half, %d over"), Shaded, TerrainTile::GridVerts, Whole));
    TestTrue(TEXT("and the tile is not uniform: some ground shaded, some lit"), Shaded > 0 && Whole > 0);
    for (int32 S = 0; S < TerrainTile::SkirtVerts; ++S)
    {
        const int32 V = TerrainTile::GridVerts + S;
        TestEqual(TEXT("a skirt vertex carries its grid vertex's shadow"), TerrainTile::UV0Of(Cast, V).X, TerrainTile::UV0Of(Cast, TerrainTile::GridOf(V)).X);
    }

    // The edges. Two tiles of one level share their edge's directions, heights
    // and footprint, so their shadows there are equal to the bit. Across a 2:1
    // edge they are not: the finer tile shadows at half the coarser's
    // footprint, and its odd edge vertices carry values where the coarser
    // side is drawn as the mean of its two. That step is measured here, not
    // bounded, and Task 10 looks for it in the frames.
    constexpr int32 N1 = TerrainTile::Cells + 1;
    const auto SideVertex = [](int32 Side, int32 T)
    {
        switch (Side)
        {
        case 0: return T * N1;
        case 1: return T * N1 + TerrainTile::Cells;
        case 2: return T;
        default: return TerrainTile::Cells * N1 + T;
        }
    };
    const auto ShadowAt = [&](const FTileBuild& Tile, const FVector3d& D) -> TOptional<float>
    {
        for (int32 Side = 0; Side < 4; ++Side)
        {
            for (int32 T = 0; T < N1; ++T)
            {
                const int32 V = SideVertex(Side, T);
                if (Tile.Directions[V] == D)
                {
                    return Tile.SunVisible[V];
                }
            }
        }
        return {};
    };
    const TArray<FTileKey, TFixedAllocator<4>> Neighbours = TerrainQuadtree::EdgeNeighbours(Key);
    const FTileKey* Across = Neighbours.FindByPredicate([&](const FTileKey& N) { return N.Face == Key.Face; });
    if (TestNotNull(TEXT("the tile has a neighbour on its own face"), Across))
    {
        const FTileBuild Same = TerrainTile::Build(*Field, *Across, Shadow);
        int32 Shared = 0;
        int32 Unequal = 0;
        FVector3d Middle = FVector3d::ZeroVector;
        for (int32 Side = 0; Side < 4; ++Side)
        {
            for (int32 T = 0; T < N1; ++T)
            {
                const int32 V = SideVertex(Side, T);
                if (const TOptional<float> There = ShadowAt(Cast, Same.Directions[V]))
                {
                    ++Shared;
                    Unequal += *There == Same.SunVisible[V] ? 0 : 1;
                    Middle += Same.Directions[V];
                }
            }
        }
        TestEqual(TEXT("a same-level neighbour shares a whole edge"), Shared, N1);
        TestEqual(TEXT("and its shadows there, to the bit"), Unequal, 0);

        // The finer tile: the level-7 child of the neighbour at the shared edge's middle.
        const FVector3d Inside = (Middle.GetSafeNormal() * 0.99 + TerrainQuadtree::CentreDirection(*Across) * 0.01).GetSafeNormal();
        const FTileBuild Fine = TerrainTile::Build(*Field, TerrainQuadtree::KeyAt(Inside, Key.Level + 1), Shadow);
        int32 Found = INDEX_NONE;
        for (int32 Side = 0; Side < 4 && Found == INDEX_NONE; ++Side)
        {
            Found = ShadowAt(Cast, Fine.Directions[SideVertex(Side, 0)]) && ShadowAt(Cast, Fine.Directions[SideVertex(Side, 2)]) ? Side : INDEX_NONE;
        }
        if (TestTrue(TEXT("the finer tile meets the coarser along one side"), Found != INDEX_NONE))
        {
            int32 Evens = 0;
            double StepSum = 0.0;
            double StepMax = 0.0;
            for (int32 T = 0; T < N1; ++T)
            {
                const FVector3d& D = Fine.Directions[SideVertex(Found, T)];
                TOptional<float> Coarse = ShadowAt(Cast, D);
                if (Coarse)
                {
                    ++Evens;
                }
                else
                {
                    const TOptional<float> Before = ShadowAt(Cast, Fine.Directions[SideVertex(Found, T - 1)]);
                    const TOptional<float> After = ShadowAt(Cast, Fine.Directions[SideVertex(Found, T + 1)]);
                    Coarse = Before && After ? TOptional<float>(0.5f * (*Before + *After)) : TOptional<float>();
                }
                if (Coarse)
                {
                    const double Step = FMath::Abs(Fine.SunVisible[SideVertex(Found, T)] - *Coarse);
                    StepSum += Step;
                    StepMax = FMath::Max(StepMax, Step);
                }
            }
            TestEqual(TEXT("the finer edge meets every other coarse vertex"), Evens, TerrainTile::Cells / 2 + 1);
            AddInfo(FString::Printf(TEXT("2:1 edge, level 6 to 7 under a 2-degree sun: shade steps by %.3f on average, %.3f at most, over %d vertices"),
                StepSum / N1, StepMax, N1));
            TestTrue(TEXT("and the step is measured"), FMath::IsFinite(StepMax));
        }
    }

    // Without a light, whole.
    bool bAllWhole = Lit.SunVisible.Num() == TerrainTile::GridVerts;
    for (const float Seen : Lit.SunVisible)
    {
        bAllWhole = bAllWhole && Seen == 1.0f;
    }
    TestTrue(TEXT("a tile built without a light has every vertex whole"), bAllWhole);
    TestEqual(TEXT("and UV0.y is unused"), TerrainTile::UV0Of(Cast, 7).Y, 0.0f);
    return true;
}

/**
 * AWorldGround builds its tiles under the sky's own light for the world
 * under the ship (SkyProjection::SunLightOf), the real ground's steepest
 * slope and ds.Terrain.ShadowSamples, and rebuilds when ds.Terrain.Shadows
 * changes, letting go of what was building without waiting and without
 * oversubscribing its workers. 800 km over Baemsekai IV only the prefetch
 * chain is wanted, so the test builds a handful of tiles.
 */
bool FGroundShadowLightTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    // The tiles' shadows only: no map is baked behind this test.
    FSkyWorld Test(TEXT("GroundShadowLightWorld"), 8, EShadows::Tiles);
    Test.BeginPlay();
    const FSkySystem Here = LocalSystem::Here(Test.World);
    if (!TestTrue(TEXT("the start system has a fourth body"), Here.Bodies.IsValidIndex(4)))
    {
        return false;
    }
    const FSkyBody& Fourth = Here.Bodies[4];
    const FVector Out = (Test.Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
    Test.Ship->PlaceShip(Fourth.Position + Out * (Fourth.Radius + 8.0e7), FRotationMatrix::MakeFromX(-Out).ToQuat());
    Test.Step(1.0f / 60.0f);
    Test.Ground->FlushBuildsForTest();
    Test.Step(1.0f / 60.0f);

    const TerrainTile::FTileShadow& Shadow = Test.Ground->GetTileShadow();
    const SunShadow::FSunLight Sky = SkyProjection::SunLightOf(Here, 4);
    TestTrue(TEXT("the tiles are built under the sky's own light for this world"),
        Shadow.Sun.Direction == Sky.Direction && Shadow.Sun.AngularRadius == Sky.AngularRadius);
    TestEqual(TEXT("with the real ground's steepest slope"), Shadow.SteepestSlope, SunShadow::SteepestSlope(Fourth.Relief));
    TestEqual(TEXT("and ds.Terrain.ShadowSamples' samples"), Shadow.Samples, SunShadow::DefaultSamples);

    const FTileKey Key = TerrainQuadtree::KeyAt(FVector3d(Out), 8);
    const FTileBuild* Resident = Test.Ground->GetResidentTile(Key);
    if (!TestNotNull(TEXT("the level-8 tile under the ship is resident"), Resident))
    {
        return false;
    }
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    TestTrue(TEXT("and carries the shadow a fresh build under that light does"),
        Resident->SunVisible == TerrainTile::Build(*Field, Key, Shadow).SunVisible);

    {
        // Off: the ground starts again, and every vertex is whole. The builds
        // in flight when it restarts are let go, not waited on, and they
        // count against the cap until they finish.
        FScopedCVar Samples(TEXT("ds.Terrain.ShadowSamples"), 13.0f);
        Test.Step(1.0f / 60.0f);   // a restart under a new sample count: builds launch
        const int32 Cap = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Terrain.BuildTasks"))->GetInt();
        FScopedCVar Off(TEXT("ds.Terrain.Shadows"), 0.0f);
        Test.Step(1.0f / 60.0f);   // and another, with those still building
        TestTrue(FString::Printf(TEXT("a restart never oversubscribes the workers (%d building, %d let go, cap %d)"),
            Test.Ground->GetBuildingCount(), Test.Ground->GetDrainingCount(), Cap),
            Test.Ground->GetBuildingCount() + Test.Ground->GetDrainingCount() <= Cap);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        TestFalse(TEXT("with ds.Terrain.Shadows 0 the ground builds under no light"), Test.Ground->GetTileShadow().Sun.IsSet());
        const FTileBuild* Rebuilt = Test.Ground->GetResidentTile(Key);
        bool bWhole = Rebuilt != nullptr;
        for (int32 V = 0; bWhole && V < Rebuilt->SunVisible.Num(); ++V)
        {
            bWhole = Rebuilt->SunVisible[V] == 1.0f;
        }
        TestTrue(TEXT("and the rebuilt tile is whole everywhere"), bWhole);
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run them to see them fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails, because `TerrainTile::FTileShadow`, `FTileBuild::SunVisible`, `UV0Of`, `AWorldGround::GetTileShadow` and `SkyTestWorld::EShadows` are undeclared.

- [ ] **Step 3: The tile.** In `Source/DeepSpace/Surface/TerrainTile.h`, add `#include <atomic>` before `#include "CoreMinimal.h"`, and `#include "Surface/SunShadow.h"` after `#include "Surface/TerrainQuadtree.h"`. In `struct FTileBuild`, after `double SkirtDepthCm = 0.0;`, add:

```cpp
    /** Each grid vertex's share of its star's disc seen past the ground --
     *  the cast shadow, SunShadow::Visible at the vertex's own height and the
     *  tile's spacing (the developer's ruling on slice (b)'s build: baked,
     *  not marched) -- 1 everywhere for a tile built without a light. The
     *  skirt's vertices carry their grid vertex's (UV0Of). */
    TArray<float> SunVisible;

    /** How much of BuildSeconds the shadow took. */
    double ShadowSeconds = 0.0;
```

In `namespace TerrainTile`, replace the `Build` declaration with:

```cpp
    /** The light a tile's shadow is cast by, the day exit's slope and the
     *  march's samples: AWorldGround's, from SkyProjection::SunLightOf and
     *  SunShadow::SteepestSlope. An unset light casts nothing. */
    struct FTileShadow
    {
        SunShadow::FSunLight Sun;
        double SteepestSlope = 0.0;
        int32 Samples = SunShadow::DefaultSamples;
    };

    /** The tile for Key: heights at the tile's own spacing as the footprint,
     *  so a coarse tile never samples fine bands into vertex noise; normals
     *  from the analytic gradient of the same band-limited height; and each
     *  grid vertex's cast shadow under Shadow's light. Pure and thread-safe
     *  given a thread-safe Ground. Cancel, if given, is polled once a grid
     *  row of the shadow; set, the build returns at once with the shadow
     *  unfinished -- a tile only a let-go build makes, whose result nobody
     *  reads (AWorldGround::Detach). */
    DEEPSPACE_API FTileBuild Build(const IGroundField& Ground, const FTileKey& Key, const FTileShadow& Shadow = FTileShadow(),
                                   const std::atomic<bool>* Cancel = nullptr);
```

After `UV2Of`'s declaration, add:

```cpp
    /** UV0: the vertex's cast shadow in x (M_SkyGround's per-vertex shadow),
     *  0 in y. UV0 was allocated full-precision and unused, so the shadow
     *  costs the mesh no memory. */
    DEEPSPACE_API FVector2f UV0Of(const FTileBuild& Tile, int32 Vertex);
```

In `Source/DeepSpace/Surface/TerrainTile.cpp`, change the definition's signature to:

```cpp
FTileBuild TerrainTile::Build(const IGroundField& Ground, const FTileKey& Key, const FTileShadow& Shadow, const std::atomic<bool>* Cancel)
```

After the grid loop (the `for (int32 J ...)` block that fills heights) and before `// The edges' own interpolation error`, add:

```cpp
    // The cast shadow at every grid vertex, from its own height at the
    // tile's spacing: the heights just read are the march's starting point.
    const double ShadowStart = FPlatformTime::Seconds();
    Tile.SunVisible.Init(1.0f, GridVerts);
    if (Shadow.Sun.IsSet())
    {
        for (int32 V = 0; V < GridVerts; ++V)
        {
            if (Cancel && V % (Cells + 1) == 0 && Cancel->load(std::memory_order_relaxed))
            {
                return Tile;   // let go: nobody reads this tile
            }
            Tile.SunVisible[V] = static_cast<float>(SunShadow::Visible(Ground, Tile.Directions[V], Shadow.Sun, Tile.SpacingCm,
                Shadow.SteepestSlope, Shadow.Samples, Tile.Heights[V]).Visible);
        }
    }
    Tile.ShadowSeconds = FPlatformTime::Seconds() - ShadowStart;
```

At the end of the file, add:

```cpp
FVector2f TerrainTile::UV0Of(const FTileBuild& Tile, int32 Vertex)
{
    const int32 G = GridOf(Vertex);
    return FVector2f(Tile.SunVisible.IsValidIndex(G) ? Tile.SunVisible[G] : 1.0f, 0.0f);
}
```

- [ ] **Step 4: The mesh.** In `Source/DeepSpace/Surface/TerrainTileComponent.cpp`, replace

```cpp
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 0, FVector2f::ZeroVector);
```

with

```cpp
                // UV0.x: the vertex's cast shadow (TerrainTile::UV0Of).
                Buffers.StaticMeshVertexBuffer.SetVertexUV(V, 0, TerrainTile::UV0Of(Tile, V));
```

- [ ] **Step 5: The ground's light.** In `Source/DeepSpace/Surface/WorldGround.h`, in the public section after `GetGroundMaterialInstance`, add:

```cpp
    /** The light, steepest slope and samples every tile of this ground is
     *  built under: the sky's own light for the world (SkyProjection::
     *  SunLightOf), unset while ds.Terrain.Shadows is 0. */
    const TerrainTile::FTileShadow& GetTileShadow() const { return TileShadow; }

    /** Builds in flight, and builds let go by a restart that have not yet
     *  finished: together never more than ds.Terrain.BuildTasks. */
    int32 GetBuildingCount() const { return InFlight.Num(); }
    int32 GetDrainingCount() const { return Draining.Num(); }

    /** The bytes the resident cut's vertex shadows hold on the CPU, read
     *  from the arrays: each resident tile's, and each pooled component's
     *  kept copy (bKeepForTest, for proxy recreation). */
    int64 GetTileShadowBytes() const;
```

After `FWorldReliefParams GroundParams;`, add:

```cpp
    TerrainTile::FTileShadow TileShadow;

    /** Builds let go by Release or EndPlay: cancelled, never waited on, and
     *  counted against ds.Terrain.BuildTasks until they finish. Their
     *  lambdas hold only the field, the key, the light and the flag -- never
     *  this -- so the actor may go before they do. */
    TArray<UE::Tasks::TTask<FTileBuild>> Draining;
    TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> BuildCancel;

    /** Cancel and let go of every build in flight. */
    void Detach();
```

Add `#include <atomic>` to `WorldGround.h`.

In `Source/DeepSpace/Surface/WorldReliefParams.h`, after `struct FWorldReliefParams`, add:

```cpp
/** The same ground: every fact FWorldRelief is built from is equal. The
 *  ground restarts on a change (AWorldGround), and the sky re-bakes that
 *  world's shadow map (AShipSky's map keys). */
inline bool SameRelief(const FWorldReliefParams& A, const FWorldReliefParams& B)
{
    return A.SeedOffset == B.SeedOffset && A.RadiusCm == B.RadiusCm && A.PeakCm == B.PeakCm
        && A.Cratering == B.Cratering && A.Ground == B.Ground;
}
```

and delete the identical `SameRelief` from `WorldGround.cpp`'s anonymous namespace (a copy there would make every call ambiguous).

In `Source/DeepSpace/Surface/WorldGround.cpp`, add `#include "Sky/SkyProjection.h"` and `#include "Surface/SunShadow.h"` with the other includes. Replace the bodies' waits:

- In `EndPlay`, replace the `for (FPending& Pending : InFlight) { Pending.Task.Wait(); }` loop and `InFlight.Reset();` with `Detach(); Draining.Reset();`. The handles are dropped, and the tasks finish on their own within a row.
- In `Release`, replace the same loop and `InFlight.Reset();` with `Detach();`.

Add:

```cpp
void AWorldGround::Detach()
{
    // Before the shadow a build was ~5 ms, and waiting was cheap. With it, a
    // build is 16-70 ms, and Release runs on every fold opened near a world.
    if (BuildCancel.IsValid())
    {
        BuildCancel->store(true, std::memory_order_relaxed);
    }
    BuildCancel.Reset();
    for (FPending& Pending : InFlight)
    {
        Draining.Add(MoveTemp(Pending.Task));
    }
    InFlight.Reset();
}

int64 AWorldGround::GetTileShadowBytes() const
{
    int64 Bytes = 0;
    for (const TPair<FTileKey, FResident>& Pair : Resident)
    {
        Bytes += Pair.Value.Tile.SunVisible.GetAllocatedSize();
    }
    for (const TObjectPtr<UPrimitiveComponent>& Pooled : Pool)
    {
        const UTerrainTileComponent* Tile = Cast<UTerrainTileComponent>(Pooled.Get());
        if (const FTileBuild* Kept = Tile ? Tile->GetTileForTest() : nullptr)
        {
            Bytes += Kept->SunVisible.GetAllocatedSize();
        }
    }
    return Bytes;
}
```

In `FlushBuildsForTest`, as its first statement, add:

```cpp
    // Tests only: what a restart let go finishes before the cut is timed.
    for (UE::Tasks::TTask<FTileBuild>& Task : Draining)
    {
        Task.Wait();
    }
    Draining.Reset();
```

In `WorldGround.cpp`'s anonymous namespace, after `CVarShow`, add:

```cpp
    TAutoConsoleVariable<int32> CVarShadows(
        TEXT("ds.Terrain.Shadows"), 1,
        TEXT("1: every tile's vertices carry the cast shadow, computed as the tile is built, off the game thread; 0 builds them without it. Changing it rebuilds the ground."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowSamples(
        TEXT("ds.Terrain.ShadowSamples"), SunShadow::DefaultSamples,
        TEXT("How many samples each vertex's march toward the sun takes (2-64). Changing it rebuilds the ground."),
        ECVF_Default);

    /** What this frame's tiles are built under: the sky's light for the
     *  world, its ground's steepest slope, the samples; nothing when off. */
    TerrainTile::FTileShadow ShadowFor(const FSkySystem& System, int32 Index)
    {
        TerrainTile::FTileShadow Shadow;
        if (CVarShadows.GetValueOnGameThread() != 0)
        {
            Shadow.Sun = SkyProjection::SunLightOf(System, Index);
            Shadow.SteepestSlope = SunShadow::SteepestSlope(System.Bodies[Index].Relief);
            Shadow.Samples = FMath::Clamp(CVarShadowSamples.GetValueOnGameThread(), 2, 64);
        }
        return Shadow;
    }

    bool SameShadow(const TerrainTile::FTileShadow& A, const TerrainTile::FTileShadow& B)
    {
        return A.Sun.Direction == B.Sun.Direction && A.Sun.AngularRadius == B.Sun.AngularRadius
            && A.SteepestSlope == B.SteepestSlope && A.Samples == B.Samples;
    }
```

In `SyncTo`, replace the nearest-world loop and the restart that follows it, from `const FSkyBody* Near = nullptr;` through the closing brace of the `if (Near->Id != Body || ...)` block, with:

```cpp
    const FSkyBody* Near = nullptr;
    int32 NearIndex = INDEX_NONE;
    double NearAltitude = TNumericLimits<double>::Max();
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        const FSkyBody& Candidate = System.Bodies[Index];
        if (Candidate.Ground == EGround::Solid)
        {
            const double Altitude = ShipAt.DistanceTo(Candidate.Position) - Candidate.Radius;
            if (Altitude < NearAltitude)
            {
                NearAltitude = Altitude;
                Near = &Candidate;
                NearIndex = Index;
            }
        }
    }
    if (!Near || NearAltitude > TerrainQuadtree::PrefetchAltitudeCm)
    {
        Release();
        return;
    }
    // A new world, this one reloaded with new priors, or its tiles' shadow
    // switched, resampled or lit otherwise: start again.
    const TerrainTile::FTileShadow Shadow = ShadowFor(System, NearIndex);
    if (Near->Id != Body || !Ground.IsValid() || !SameRelief(Near->Relief, GroundParams) || !SameShadow(Shadow, TileShadow))
    {
        Release();
        Body = Near->Id;
        GroundParams = Near->Relief;
        Ground = ShipGround::FromRelief(Near->Relief);
        TileShadow = Shadow;
    }
```

In `Launch`, replace its first line

```cpp
    const int32 Slots = FMath::Max(1, CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num();
```

with

```cpp
    // What a restart let go still holds a worker until it finishes.
    Draining.RemoveAll([](const UE::Tasks::TTask<FTileBuild>& Task) { return Task.IsCompleted(); });
    const int32 Slots = FMath::Max(1, CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num() - Draining.Num();
```

and replace

```cpp
        const FGroundFieldRef Field = Ground;
        InFlight.Add(FPending{ Key, UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Field, Key]() { return TerrainTile::Build(*Field, Key); }, UE::Tasks::ETaskPriority::BackgroundNormal) });
```

with

```cpp
        const FGroundFieldRef Field = Ground;
        const TerrainTile::FTileShadow Shadow = TileShadow;
        if (!BuildCancel.IsValid())
        {
            BuildCancel = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
        }
        const TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> Cancel = BuildCancel;
        InFlight.Add(FPending{ Key, UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Field, Key, Shadow, Cancel]() { return TerrainTile::Build(*Field, Key, Shadow, Cancel.Get()); },
            UE::Tasks::ETaskPriority::BackgroundNormal) });
```

In `Describe`, after the first `FString Out = FString::Printf(...)` line (the world, the morph, the counts) and before the per-level loop, add:

```cpp
    Out += FString::Printf(TEXT("  cast shadow: %s, sun %.3f deg in radius, %d samples, day exit %.1f deg\n"),
        TileShadow.Sun.IsSet() ? TEXT("on") : TEXT("off"), FMath::RadiansToDegrees(TileShadow.Sun.AngularRadius), TileShadow.Samples,
        FMath::RadiansToDegrees(FMath::Atan(TileShadow.SteepestSlope)));
```

- [ ] **Step 6: Test worlds build without the shadow unless asked.** In `Source/DeepSpace/Tests/SkyTestWorld.h`, before `struct FSkyWorld`, add:

```cpp
    /** Whether a test world casts shadows: Tiles marches every tile's
     *  vertices (ds.Terrain.Shadows), Maps bakes every solid world's map
     *  (ds.Sky.ShadowMaps), On both (the cast-shadow plan). They are the
     *  costliest things a test world does and most tests never look at one,
     *  so they are off unless a test asks, and put back after. */
    enum class EShadows : uint8
    {
        Off = 0,
        Tiles = 1,
        Maps = 2,
        On = 3
    };
```

Change the constructor's signature to `explicit FSkyWorld(const TCHAR* Name, int32 DistantStarCount = 8, EShadows Shadows = EShadows::Off)`. As its first statement, before `World = UWorld::CreateWorld(...)`, add:

```cpp
            // Before any actor exists: the ground and the sky read these when
            // they first build. The tiles and the maps are asked for apart,
            // so a test of one never pays for the other in the background.
            const TPair<const TCHAR*, EShadows> Switches[] = { { TEXT("ds.Terrain.Shadows"), EShadows::Tiles }, { TEXT("ds.Sky.ShadowMaps"), EShadows::Maps } };
            for (const TPair<const TCHAR*, EShadows>& Switch : Switches)
            {
                if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Switch.Key))
                {
                    ShadowSwitchesWere.Add(Variable, Variable->GetString());
                    const bool bOn = (static_cast<uint8>(Shadows) & static_cast<uint8>(Switch.Value)) != 0;
                    Variable->Set(bOn ? 1 : 0, ECVF_SetByCode);
                }
            }
```

After the `Sphere` member, add:

```cpp
        /** The shadow switches as they were before this world set them. */
        TMap<IConsoleVariable*, FString> ShadowSwitchesWere;
```

At the end of `~FSkyWorld()`, after `World->DestroyWorld(false);`, add:

```cpp
            for (const TPair<IConsoleVariable*, FString>& Was : ShadowSwitchesWere)
            {
                Was.Key->Set(*Was.Value, ECVF_SetByCode);
            }
```

- [ ] **Step 7: Run them to see them pass, and the ground's tests beside them.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.TileSunShadow && ./test.sh DeepSpace.Surface.GroundShadowLight && ./test.sh DeepSpace.Surface`

Expected: both PASS, and every `DeepSpace.Surface.*` test still passes.
- `.TileComponent` builds its tile without a light: whole, and UV0 (1, 0).
- `.GroundActor` and the rest build in `FSkyWorld`, now with the shadow off, so they cost what they did.
- Copy `.TileSunShadow`'s two info lines into the task report: the tile's shaded counts, and the 2:1 edge's step. The step goes to the developer with Task 0's answers, and Task 10 looks for it in the frames.
- If that tile turns out uniform, move it to another fixture direction and record which. Do not drop the check.
- If the same-level edge is not equal to the bit, two tiles compute one direction differently (`GridDirection`, or a height read at another footprint). That is a bug, not a tolerance to widen.

- [ ] **Step 8: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Surface/TerrainTile.h Source/DeepSpace/Surface/TerrainTile.cpp \
  Source/DeepSpace/Surface/TerrainTileComponent.cpp Source/DeepSpace/Surface/WorldGround.h Source/DeepSpace/Surface/WorldGround.cpp \
  Source/DeepSpace/Surface/WorldReliefParams.h Source/DeepSpace/Tests/SkyTestWorld.h Source/DeepSpace/Tests/TileSunShadowTest.cpp && \
git commit -qm "feat(terrain): every tile's vertices carry the cast shadow, computed as the tile is built

TerrainTile::Build marches SunShadow::Visible at each grid vertex's own
height and the tile's spacing under the sky's own light (SunLightOf); the
skirts copy theirs; UV0.x carries it (full precision, already allocated).
AWorldGround rebuilds when the light, ds.Terrain.Shadows or
ds.Terrain.ShadowSamples changes, and lets go of the builds in flight
without waiting (cancelled by flag, counted against the cap until they
finish). Test worlds build without tile shadows or maps unless asked, each
apart (SkyTestWorld::EShadows). DeepSpace.Surface.TileSunShadow (with a
same-level edge held to the bit and a 2:1 edge's step measured),
.GroundShadowLight.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 9: Prove them.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainTile.cpp 'SunShadow::Visible(Ground, Tile.Directions[V], Shadow.Sun, Tile.SpacingCm,' 'SunShadow::Visible(Ground, Tile.Directions[V], Shadow.Sun, 2.0 * Tile.SpacingCm,' 'DeepSpace.Surface.TileSunShadow$'; \
Tools/mutate.sh Source/DeepSpace/Surface/TerrainTile.cpp 'return FVector2f(Tile.SunVisible.IsValidIndex(G) ? Tile.SunVisible[G] : 1.0f, 0.0f);' 'return FVector2f(Tile.SunVisible.IsValidIndex(Vertex) ? Tile.SunVisible[Vertex] : 1.0f, 0.0f);' 'DeepSpace.Surface.TileSunShadow$'; \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp '            Shadow.Sun = SkyProjection::SunLightOf(System, Index);' '            Shadow.Sun = SkyProjection::SunLightOf(System, Index - 1);' 'DeepSpace.Surface.GroundShadowLight$'; \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp '!SameShadow(Shadow, TileShadow))' '!SameShadow(Shadow, Shadow))' 'DeepSpace.Surface.GroundShadowLight$'; \
Tools/mutate.sh Source/DeepSpace/Surface/WorldGround.cpp 'CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num() - Draining.Num();' 'CVarBuildTasks.GetValueOnGameThread()) - InFlight.Num();' 'DeepSpace.Surface.GroundShadowLight$'; \
./build.sh
```

Expected: five `KILLED`.
- The shadow marched at twice the tile's spacing is not the one at its spacing.
- The skirt that reads its own index is out of range, and so whole.
- The light of the body before is another world's.
- The ground that never restarts keeps its shadowed tiles with the switch at 0.
- The cap that forgets what it let go launches a full set beside the builds still draining.

That `Release` and `EndPlay` no longer wait is a property of the code, not of a value, so no headless mutant can see it. `Eyes.ShadowBakeCost` times it (`release_ms`, Task 7). The same-level edge's equality has no mutant of its own either: it holds by construction, and the bit-equality would catch any nondeterminism.

---

## Task 5: `SunShadowMap` -- the orbit's map, pure

**Owner:** T. **Depends on:** Task 2.

**Files:**
- Modify: `Shaders/Private/WorldRelief.ush`. Add three shims to each half, and the map's lookup at the end of the file.
- Modify: `Source/DeepSpace/Surface/WorldRelief.h`, `Source/DeepSpace/Surface/WorldRelief.cpp`. Add `WorldReliefNoise::{FShadowCoord, FShadowTaps, ShadowMapCoordF64, ShadowMapCoordF32, ShadowTapsF64, ShadowTapsF32, BilinearF64, BilinearF32, LerpF64, LerpF32}`.
- Create: `Source/DeepSpace/Surface/SunShadowMap.h`, `Source/DeepSpace/Surface/SunShadowMap.cpp`
- Create: `Source/DeepSpace/Tests/SunShadowMapTest.cpp` (`DeepSpace.Surface.SunShadowMap.Shape`, `.TexelsAreTheProfile`, `.NightBelowTheMap`, `.Lookup`, `.Mips`)

**Interfaces:**
- Consumes: `SunShadow::{FSunLight, AlongProfile, MarchEnd, NightDip, SteepestSlope, Visible, SunRadiusMax}` (Task 2); `IGroundField`, `FReliefGround`; `GroundFixtures::FixtureParams`.
- Produces:

```cpp
struct FSunShadowMap
{
    int32 Width; int32 Rows; double PsiLo; double Step;
    FVector3d FrameX, FrameY, FrameZ;
    TArray<TArray<uint16>> Levels;
    int32 WidthAt(int32 Level) const; int32 RowsAt(int32 Level) const; int32 LevelCount() const;
    uint16 Texel(int32 Level, int32 X, int32 Y) const; int64 Bytes() const;
};
namespace SunShadowMap
{
    inline constexpr int32 DefaultWidth = 4096;
    FSunShadowMap Shape(const IGroundField& Ground, const SunShadow::FSunLight& Sun, int32 Width);
    FVector3d TexelDirection(const FSunShadowMap& Map, int32 Column, int32 Row);
    uint16 Quantise(double Visible);
    FSunShadowMap Bake(const IGroundField& Ground, const SunShadow::FSunLight& Sun, double SteepestSlope, int32 Width,
                       const std::atomic<bool>* Cancel = nullptr);
    void BuildLevels(FSunShadowMap& Map);
    double Sample(const FSunShadowMap& Map, const FVector3d& D, double Footprint);
    float SampleF32(const FSunShadowMap& Map, const FVector3f& D, float Footprint);
}
```

In the shared file: `WR_ShadowCoord WR_ShadowMapCoord(DX, DY, DZ, XX, XY, XZ, ZX, ZY, ZZ, PsiLo, Step, Footprint, Levels)`, `WR_ShadowTaps WR_ShadowTapsAt(U, V, Width, Rows, Level)`, `WR_REAL WR_Bilinear(A, B, C, D, FX, FY)`, `WR_PI`.

- [ ] **Step 1: Write the failing tests.** Create `Source/DeepSpace/Tests/SunShadowMapTest.cpp`:

```cpp
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/SunShadowMap.h"
#include "Surface/WorldRelief.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapShapeTest, "DeepSpace.Surface.SunShadowMap.Shape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapTexelsTest, "DeepSpace.Surface.SunShadowMap.TexelsAreTheProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapNightTest, "DeepSpace.Surface.SunShadowMap.NightBelowTheMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapLookupTest, "DeepSpace.Surface.SunShadowMap.Lookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapMipsTest, "DeepSpace.Surface.SunShadowMap.Mips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SunShadowMapTestLocal
{
    /** Baemsekai IV-like ground under a star 1.66 degrees in radius, from an
     *  oblique direction. */
    SunShadow::FSunLight Light()
    {
        SunShadow::FSunLight Sun;
        Sun.Direction = FVector3d(0.3, 0.4, FMath::Sqrt(0.75)).GetSafeNormal();
        Sun.AngularRadius = 0.02903;
        return Sun;
    }

    /** A hand-made map, 16 columns by 4 rows, every row's centre under pi/2,
     *  its frame the body's axes; each texel 1000 + 3000 x column + 700 x row. */
    FSunShadowMap Hand()
    {
        FSunShadowMap Map;
        Map.Width = 16;
        Map.Rows = 4;
        Map.PsiLo = -0.1;
        Map.Step = 2.0 * UE_DOUBLE_PI / 16.0;
        Map.Levels.SetNum(1);
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            for (int32 Column = 0; Column < Map.Width; ++Column)
            {
                Map.Levels[0].Add(static_cast<uint16>(1000 + 3000 * Column + 700 * Row));
            }
        }
        SunShadowMap::BuildLevels(Map);
        return Map;
    }

    /** The direction at azimuth Phi and elevation Psi in the map's frame. */
    FVector3d At(const FSunShadowMap& Map, double Phi, double Psi)
    {
        return (Map.FrameX * FMath::Cos(Phi) + Map.FrameY * FMath::Sin(Phi)) * FMath::Cos(Psi) + Map.FrameZ * FMath::Sin(Psi);
    }
}

bool FSunShadowMapShapeTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    const FReliefGround Ground(GroundFixtures::FixtureParams());
    const SunShadow::FSunLight Sun = Light();
    const FSunShadowMap Map = SunShadowMap::Shape(Ground, Sun, 64);
    const double R = Ground.RadiusCm();
    TestEqual(TEXT("64 columns"), Map.Width, 64);
    TestTrue(TEXT("each 2 pi / 64 of azimuth"), FMath::IsNearlyEqual(Map.Step, 2.0 * UE_DOUBLE_PI / 64.0, 1e-15));
    const double PsiLo = -(SunShadow::NightDip(R, Ground.MaxHeightCm(), Ground.MinHeightCm()) + Sun.AngularRadius);
    TestTrue(FString::Printf(TEXT("the lowest row is where even the peak's view of the lowest ground clears the star's top (%.6f)"), Map.PsiLo),
        FMath::IsNearlyEqual(Map.PsiLo, PsiLo, 1e-15));
    TestEqual(TEXT("and the rows reach the point under the star"), Map.Rows, FMath::CeilToInt32((0.5 * UE_DOUBLE_PI - PsiLo) / Map.Step));
    TestTrue(TEXT("Z is the light"), Map.FrameZ.Equals(Sun.Direction.GetSafeNormal(), 1e-15));
    TestTrue(TEXT("the frame is orthonormal and right-handed"),
        FMath::IsNearlyEqual(Map.FrameX.Size(), 1.0, 1e-15) && FMath::Abs(FVector3d::DotProduct(Map.FrameX, Map.FrameZ)) <= 1e-15
        && FVector3d::CrossProduct(Map.FrameX, Map.FrameY).Equals(Map.FrameZ, 1e-15));
    for (int32 Row = 0; Row < Map.Rows; ++Row)
    {
        const double Psi = Map.PsiLo + (Row + 0.5) * Map.Step;
        if (Psi > 0.5 * UE_DOUBLE_PI)
        {
            continue;   // the last row's centre may lie just past the pole, where no lookup lands on it
        }
        for (int32 Column = 0; Column < Map.Width; ++Column)
        {
            const FVector3d D = SunShadowMap::TexelDirection(Map, Column, Row);
            const double Phi = FMath::Atan2(FVector3d::DotProduct(D, Map.FrameY), FVector3d::DotProduct(D, Map.FrameX));
            TestTrue(TEXT("a texel's elevation is its row's centre"), FMath::IsNearlyEqual(FMath::Asin(FVector3d::DotProduct(D, Map.FrameZ)), Psi, 1e-12));
            TestTrue(TEXT("and its azimuth its column's"), FMath::IsNearlyEqual(Phi, -UE_DOUBLE_PI + (Column + 0.5) * Map.Step, 1e-12));
        }
    }
    TestEqual(TEXT("no star, no map"), SunShadowMap::Shape(Ground, SunShadow::FSunLight(), 64).Width, 0);
    return true;
}

bool FSunShadowMapTexelsTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    // Every texel is AlongProfile over its column's heights ahead of it, read
    // once each at the step, quantised: worked out again here, column by
    // column, and held equal to the bit.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Ground(Params);
    const SunShadow::FSunLight Sun = Light();
    const double Steep = SunShadow::SteepestSlope(Params);
    const FSunShadowMap Map = SunShadowMap::Bake(Ground, Sun, Steep, 128);
    if (!TestEqual(TEXT("level 0 holds every texel"), Map.LevelCount() > 0 ? Map.Levels[0].Num() : 0, Map.Width * Map.Rows))
    {
        return false;
    }
    const double R = Ground.RadiusCm();
    int32 Wrong = 0;
    int32 Shaded = 0;
    for (int32 Column = 0; Column < Map.Width; Column += 7)
    {
        TArray<double> Heights;
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            Heights.Add(Ground.Height(SunShadowMap::TexelDirection(Map, Column, Row), Map.Step * R));
        }
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            const double Psi = Map.PsiLo + (Row + 0.5) * Map.Step;
            const uint16 Want = SunShadowMap::Quantise(SunShadow::AlongProfile(R, Ground.MaxHeightCm(), Ground.MinHeightCm(), Steep,
                MakeArrayView(Heights.GetData() + Row, Map.Rows - Row), Map.Step, Psi, Sun.AngularRadius).Visible);
            Wrong += Map.Texel(0, Column, Row) == Want ? 0 : 1;
            Shaded += Want < 32768 ? 1 : 0;
        }
    }
    TestEqual(TEXT("every texel is its column's profile, quantised"), Wrong, 0);
    AddInfo(FString::Printf(TEXT("128 columns x %d rows; of the columns checked, %d texels under half"), Map.Rows, Shaded));
    TestEqual(TEXT("a whole texel is 65535"), static_cast<int32>(SunShadowMap::Quantise(1.0)), 65535);
    TestEqual(TEXT("a dark one 0"), static_cast<int32>(SunShadowMap::Quantise(0.0)), 0);
    std::atomic<bool> Stop(true);
    TestEqual(TEXT("a cancelled bake stops and returns nothing"), SunShadowMap::Bake(Ground, Sun, Steep, 128, &Stop).LevelCount(), 0);
    return true;
}

bool FSunShadowMapNightTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    // The rows the map leaves out are dark for every point of the world,
    // whatever its height: the march itself says so at every footprint.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Ground(Params);
    const SunShadow::FSunLight Sun = Light();
    const FSunShadowMap Shape = SunShadowMap::Shape(Ground, Sun, 256);
    const double Steep = SunShadow::SteepestSlope(Params);
    FRandomStream Stream(20260930);
    int32 Lit = 0;
    for (int32 Sample = 0; Sample < 300; ++Sample)
    {
        const FVector3d D(Stream.GetUnitVector());
        const FVector3d Toward(Stream.GetUnitVector());
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        const double Psi = Shape.PsiLo - 1.0e-6 - 0.02 * Stream.GetFraction();
        SunShadow::FSunLight Under = Sun;
        Under.Direction = D * FMath::Sin(Psi) + Level * FMath::Cos(Psi);
        for (const double Footprint : { 0.0, 1.0e4, Shape.Step * Ground.RadiusCm() })
        {
            Lit += SunShadow::Visible(Ground, D, Under, Footprint, Steep).Visible > 0.0 ? 1 : 0;
        }
    }
    TestEqual(TEXT("under the map's lowest row no point sees any of its star"), Lit, 0);
    return true;
}

bool FSunShadowMapLookupTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    const FSunShadowMap Map = Hand();
    const auto Value = [&](int32 Level, int32 X, int32 Y) { return Map.Texel(Level, X, Y) / 65535.0; };
    // At every texel's centre, with a footprint finer than a texel, the lookup is the texel.
    for (int32 Row = 0; Row < Map.Rows; ++Row)
    {
        for (int32 Column = 0; Column < Map.Width; ++Column)
        {
            const FVector3d D = SunShadowMap::TexelDirection(Map, Column, Row);
            TestTrue(FString::Printf(TEXT("the texel at %d, %d, in double"), Column, Row),
                FMath::IsNearlyEqual(SunShadowMap::Sample(Map, D, 1.0e-9), Value(0, Column, Row), 1e-9));
            TestTrue(FString::Printf(TEXT("and in float, %d, %d"), Column, Row),
                FMath::IsNearlyEqual(static_cast<double>(SunShadowMap::SampleF32(Map, FVector3f(D), 1.0e-9f)), Value(0, Column, Row), 2e-6));
        }
    }
    // The seam: azimuth +-pi lies between the last column and the first, from either side.
    const double RowOne = Map.PsiLo + 1.5 * Map.Step;
    const double Before = SunShadowMap::Sample(Map, At(Map, UE_DOUBLE_PI - 1.0e-7, RowOne), 1.0e-9);
    const double After = SunShadowMap::Sample(Map, At(Map, -UE_DOUBLE_PI + 1.0e-7, RowOne), 1.0e-9);
    const double Between = 0.5 * (Value(0, 15, 1) + Value(0, 0, 1));
    TestTrue(FString::Printf(TEXT("just short of +pi the lookup is the last and first columns' mean (%.8f, %.8f)"), Before, Between),
        FMath::IsNearlyEqual(Before, Between, 1e-5));
    TestTrue(FString::Printf(TEXT("and just past -pi it is the same (%.8f)"), After), FMath::IsNearlyEqual(After, Between, 1e-5));
    // Under PsiLo no point of the world sees any of the star (the night
    // exit's proof), so the lookup is 0 there -- never row 0's value, which a
    // relief normal tilted toward the sun past the terminator would otherwise
    // be lit by. Between PsiLo and row 0's centre the rows clamp to row 0;
    // over the highest, the highest.
    const double Column3 = -UE_DOUBLE_PI + 3.5 * Map.Step;
    TestTrue(TEXT("under PsiLo the lookup is 0, in double"), SunShadowMap::Sample(Map, At(Map, Column3, -0.3), 1.0e-9) == 0.0);
    TestTrue(TEXT("and in float"), SunShadowMap::SampleF32(Map, FVector3f(At(Map, Column3, -0.3)), 1.0e-9f) == 0.0f);
    TestTrue(TEXT("and just under it, at every level"), SunShadowMap::Sample(Map, At(Map, Column3, Map.PsiLo - 1.0e-6), 0.5) == 0.0);
    TestTrue(TEXT("just over PsiLo the lowest row"),
        FMath::IsNearlyEqual(SunShadowMap::Sample(Map, At(Map, Column3, Map.PsiLo + 0.25 * Map.Step), 1.0e-9), Value(0, 3, 0), 1e-9));
    TestTrue(TEXT("over the highest the highest"), FMath::IsNearlyEqual(SunShadowMap::Sample(Map, At(Map, Column3, 1.45), 1.0e-9), Value(0, 3, 3), 1e-9));
    // The level: a footprint twice a texel at the terminator reads level 1.
    const double RowZero = Map.PsiLo + 0.5 * Map.Step;
    const FVector3d Coarse = SunShadowMap::TexelDirection(Map, 2, 0);
    const WorldReliefNoise::FShadowCoord Two = WorldReliefNoise::ShadowMapCoordF64(Coarse, Map.FrameX, Map.FrameZ, Map.PsiLo, Map.Step,
        2.0 * Map.Step * FMath::Cos(RowZero), Map.LevelCount());
    TestTrue(FString::Printf(TEXT("a footprint of two texels is level 1 (%d, blend %.9f)"), Two.Level0, Two.Blend),
        (Two.Level0 == 1 && Two.Blend <= 1e-9) || (Two.Level0 == 0 && Two.Blend >= 1.0 - 1e-9));
    // At level 1 texel (0, 0) covers level 0's columns 0-1, so column 2's centre is 0.75 of the way from it to (1, 0).
    const double LevelOne = WorldReliefNoise::LerpF64(Value(1, 0, 0), Value(1, 1, 0), 0.75);
    TestTrue(FString::Printf(TEXT("and reads level 1's texels there (%.8f against %.8f)"), SunShadowMap::Sample(Map, Coarse, 2.0 * Map.Step * FMath::Cos(RowZero)), LevelOne),
        FMath::IsNearlyEqual(SunShadowMap::Sample(Map, Coarse, 2.0 * Map.Step * FMath::Cos(RowZero)), LevelOne, 1e-6));
    // Toward the point under the star a texel narrows as cos(psi), so the same footprint reads coarser.
    const WorldReliefNoise::FShadowCoord High = WorldReliefNoise::ShadowMapCoordF64(At(Map, Column3, 1.27), Map.FrameX, Map.FrameZ,
        Map.PsiLo, Map.Step, Map.Step, Map.LevelCount());
    TestTrue(FString::Printf(TEXT("one texel's footprint at 1.27 rad up is level log2(1 / cos) = 1.76 (%d + %.3f)"), High.Level0, High.Blend),
        High.Level0 == 1 && FMath::IsNearlyEqual(High.Blend, FMath::Log2(1.0 / FMath::Cos(1.27)) - 1.0, 1e-9));
    // Float against double, anywhere.
    FRandomStream Stream(20261001);
    double Widest = 0.0;
    for (int32 Sample = 0; Sample < 400; ++Sample)
    {
        const FVector3d D(Stream.GetUnitVector());
        const double Footprint = FMath::Pow(10.0, Stream.FRandRange(-4.0, -0.3));
        Widest = FMath::Max(Widest, FMath::Abs(SunShadowMap::Sample(Map, D, Footprint) - SunShadowMap::SampleF32(Map, FVector3f(D), static_cast<float>(Footprint))));
    }
    TestTrue(FString::Printf(TEXT("the float lookup is the double's to 1e-5 (%.2e)"), Widest), Widest <= 1.0e-5);
    TestTrue(TEXT("an empty map is whole"), SunShadowMap::Sample(FSunShadowMap(), FVector3d::UnitZ(), 1.0e-3) == 1.0);
    return true;
}

bool FSunShadowMapMipsTest::RunTest(const FString& Parameters)
{
    // Four by three: level 1 is two by one, each the mean of its two by two
    // (rounded); level 2 is one by one, from level 1's two, its row repeated.
    FSunShadowMap Map;
    Map.Width = 4;
    Map.Rows = 3;
    Map.Levels.SetNum(1);
    Map.Levels[0] = { 0, 6, 8, 12, 100, 104, 108, 112, 60000, 60000, 60000, 60000 };
    SunShadowMap::BuildLevels(Map);
    TestEqual(TEXT("three levels, as the GPU counts a 4 x 3 texture's mips"), Map.LevelCount(), 3);
    TestEqual(TEXT("level 1 is 2 x 1"), Map.WidthAt(1) * 10 + Map.RowsAt(1), 21);
    TestTrue(TEXT("its texels are the rounded means of rows 0 and 1 (row 2 falls away, as the GPU's floor does)"),
        Map.Levels.Num() == 3 && Map.Levels[1] == TArray<uint16>({ 53, 60 }));
    TestTrue(TEXT("level 2 is their mean"), Map.Levels.Num() == 3 && Map.Levels[2] == TArray<uint16>({ 57 }));
    TestEqual(TEXT("and the bytes are every level's"), Map.Bytes(), static_cast<int64>((12 + 2 + 1) * sizeof(uint16)));
    return true;
}

#endif
```

The known means: level 1 (0, 0) is `(0 + 6 + 100 + 104 + 2) / 4 = 53` and (1, 0) is `(8 + 12 + 108 + 112 + 2) / 4 = 60`. Level 2 is `(53 + 60 + 53 + 60 + 2) / 4 = 57`. The first sum, 210, is chosen so that rounding matters: unrounded it is 52.

- [ ] **Step 2: Run them to see them fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails, because `Surface/SunShadowMap.h` does not exist.

- [ ] **Step 3: The shims and the lookup, in the shared file.** In `Shaders/Private/WorldRelief.ush`'s C++ half, after `WR_step`'s line, add:

```cpp
inline WR_REAL WR_asin(WR_REAL X) { return std::asin(X); }
inline WR_REAL WR_atan2(WR_REAL Y, WR_REAL X) { return std::atan2(Y, X); }
inline WR_REAL WR_log2(WR_REAL X) { return std::log2(X); }
```

In its HLSL half, after `#define WR_step(Edge, X) step(Edge, X)`, add:

```hlsl
#define WR_asin(X) asin(X)
#define WR_atan2(Y, X) atan2(Y, X)
#define WR_log2(X) log2(X)
```

At the end of the file, after `WR_GroundNormal`, append:

```hlsl
// -- The cast shadow's map (the developer's ruling on slice (b)'s build,
// 2026-09-28: baked, not marched) --------------------------------------------
// The orbit reads each solid world's shadow from one texture, baked in C++
// when the system loads (SunShadowMap::Bake). It is an equirectangular map in
// the star's own frame -- Z the light, as the sky lights the world, X and Y
// square to it -- its columns the azimuth about the light from -pi, its rows
// the star's elevation from PsiLo, the lowest at which any point of the world
// can see any of it, up to the point under the star. Each texel is Step
// square at the terminator, where the shadows are. M_SkyBody and M_SkyGround
// read it through these three functions and eight texel loads, and
// SunShadowMap::Sample is their C++ mirror.
static const WR_REAL WR_PI = WR_REAL(3.14159265358979323846);

struct WR_ShadowCoord
{
    WR_REAL U;       // the column coordinate, texel centres at whole numbers
    WR_REAL V;       // the row coordinate, the same
    int Level0;      // the finer of the two levels read
    int Level1;      // the coarser
    WR_REAL Blend;   // how far toward Level1
    int Night;       // 1 under PsiLo, where the lookup is 0: no point sees any of the star
};

// D in the body's axes; the frame's X and Z; PsiLo and Step, rad; Footprint
// one pixel's width, D units (the faces' filtered footprint divided by
// filter_pixels, which M_SkyBody and M_SkyGround do before the node); Levels
// the map's.
WR_ShadowCoord WR_ShadowMapCoord(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL XX, WR_REAL XY, WR_REAL XZ,
                                 WR_REAL ZX, WR_REAL ZY, WR_REAL ZZ, WR_REAL PsiLo, WR_REAL Step, WR_REAL Footprint, int Levels)
{
    const WR_REAL YX = ZY * XZ - ZZ * XY;
    const WR_REAL YY = ZZ * XX - ZX * XZ;
    const WR_REAL YZ = ZX * XY - ZY * XX;
    const WR_REAL S = WR_min(WR_REAL(1.0), WR_max(WR_REAL(-1.0), DX * ZX + DY * ZY + DZ * ZZ));
    const WR_REAL Psi = WR_asin(S);
    const WR_REAL Phi = WR_atan2(DX * YX + DY * YY + DZ * YZ, DX * XX + DY * XY + DZ * XZ);
    WR_ShadowCoord Out;
    Out.U = (Phi + WR_PI) / Step - WR_REAL(0.5);
    Out.V = (Psi - PsiLo) / Step - WR_REAL(0.5);
    Out.Night = Psi < PsiLo ? 1 : 0;
    // The pixel's footprint in texels along a texel's narrower side: along
    // the rows a texel is Step cos(psi) across, so toward the point under
    // the star the map is read coarser; a map coarser than the pixel is read
    // at its finest.
    const WR_REAL Across = Step * WR_max(WR_sqrt(WR_max(WR_REAL(0.0), WR_REAL(1.0) - S * S)), WR_REAL(1.0e-4));
    const WR_REAL Level = WR_min(WR_log2(WR_max(Footprint / Across, WR_REAL(1.0))), WR_REAL(Levels - 1));
    Out.Level0 = int(WR_floor(Level));
    Out.Level1 = Out.Level0 + 1 < Levels ? Out.Level0 + 1 : Levels - 1;
    Out.Blend = Level - WR_floor(Level);
    return Out;
}

struct WR_ShadowTaps
{
    int X0;
    int X1;
    int Y0;
    int Y1;
    WR_REAL FX;
    WR_REAL FY;
};

// The four texels around (U, V) at Level of a map Width x Rows at level 0:
// columns wrap, rows clamp, and a level's texel is level 0's halved per
// level about the texel centres, the GPU's own mip sizes (Width >> Level).
WR_ShadowTaps WR_ShadowTapsAt(WR_REAL U, WR_REAL V, int Width, int Rows, int Level)
{
    const int W = (Width >> Level) > 1 ? (Width >> Level) : 1;
    const int H = (Rows >> Level) > 1 ? (Rows >> Level) : 1;
    const WR_REAL Scale = WR_REAL(1 << Level);
    const WR_REAL UL = (U + WR_REAL(0.5)) / Scale - WR_REAL(0.5);
    const WR_REAL VL = (V + WR_REAL(0.5)) / Scale - WR_REAL(0.5);
    const WR_REAL FloorU = WR_floor(UL);
    const WR_REAL FloorV = WR_floor(VL);
    const int X = int(FloorU);
    const int Y = int(FloorV);
    WR_ShadowTaps Out;
    // X lies in [-1, W - 1]: one wrap either way is all it can need.
    Out.X0 = X < 0 ? X + W : (X >= W ? X - W : X);
    Out.X1 = X + 1 < 0 ? X + 1 + W : (X + 1 >= W ? X + 1 - W : X + 1);
    Out.Y0 = Y < 0 ? 0 : (Y > H - 1 ? H - 1 : Y);
    Out.Y1 = Y + 1 < 0 ? 0 : (Y + 1 > H - 1 ? H - 1 : Y + 1);
    Out.FX = UL - FloorU;
    Out.FY = VL - FloorV;
    return Out;
}

WR_REAL WR_Bilinear(WR_REAL A, WR_REAL B, WR_REAL C, WR_REAL D, WR_REAL FX, WR_REAL FY)
{
    return WR_Lerp(WR_Lerp(A, B, FX), WR_Lerp(C, D, FX), FY);
}
```

- [ ] **Step 4: The C++ reach into it.** In `Source/DeepSpace/Surface/WorldRelief.h`, in `namespace WorldReliefNoise` before `struct FBands`, add:

```cpp
    /** The cast shadow's map lookup, from the shared file (WR_ShadowMapCoord,
     *  WR_ShadowTapsAt, WR_Bilinear, WR_Lerp), in double and in float: the GPU's
     *  operations in the GPU's precision, for SunShadowMap::Sample and the
     *  parity test. */
    struct FShadowCoord
    {
        double U = 0.0;
        double V = 0.0;
        int32 Level0 = 0;
        int32 Level1 = 0;
        double Blend = 0.0;
        int32 Night = 0;   // under PsiLo: the lookup is 0
    };
    struct FShadowTaps
    {
        int32 X0 = 0;
        int32 X1 = 0;
        int32 Y0 = 0;
        int32 Y1 = 0;
        double FX = 0.0;
        double FY = 0.0;
    };
    DEEPSPACE_API FShadowCoord ShadowMapCoordF64(const FVector3d& D, const FVector3d& X, const FVector3d& Z, double PsiLo, double Step,
                                                 double Footprint, int32 Levels);
    DEEPSPACE_API FShadowCoord ShadowMapCoordF32(const FVector3f& D, const FVector3f& X, const FVector3f& Z, float PsiLo, float Step,
                                                 float Footprint, int32 Levels);
    DEEPSPACE_API FShadowTaps ShadowTapsF64(double U, double V, int32 Width, int32 Rows, int32 Level);
    DEEPSPACE_API FShadowTaps ShadowTapsF32(float U, float V, int32 Width, int32 Rows, int32 Level);
    DEEPSPACE_API double BilinearF64(double A, double B, double C, double D, double FX, double FY);
    DEEPSPACE_API float BilinearF32(float A, float B, float C, float D, float FX, float FY);
    DEEPSPACE_API double LerpF64(double A, double B, double T);
    DEEPSPACE_API float LerpF32(float A, float B, float T);
```

In `Source/DeepSpace/Surface/WorldRelief.cpp`, in `namespace WorldReliefLocal`, add:

```cpp
    template <typename TCoord>
    WorldReliefNoise::FShadowCoord ToShadowCoord(const TCoord& C)
    {
        WorldReliefNoise::FShadowCoord Out;
        Out.U = C.U;
        Out.V = C.V;
        Out.Level0 = C.Level0;
        Out.Level1 = C.Level1;
        Out.Blend = C.Blend;
        Out.Night = C.Night;
        return Out;
    }

    template <typename TTaps>
    WorldReliefNoise::FShadowTaps ToShadowTaps(const TTaps& T)
    {
        WorldReliefNoise::FShadowTaps Out;
        Out.X0 = T.X0;
        Out.X1 = T.X1;
        Out.Y0 = T.Y0;
        Out.Y1 = T.Y1;
        Out.FX = T.FX;
        Out.FY = T.FY;
        return Out;
    }
```

After `WorldReliefNoise::FaceF32`, add:

```cpp
WorldReliefNoise::FShadowCoord WorldReliefNoise::ShadowMapCoordF64(const FVector3d& D, const FVector3d& X, const FVector3d& Z, double PsiLo,
                                                                   double Step, double Footprint, int32 Levels)
{
    return WorldReliefLocal::ToShadowCoord(WorldReliefF64::WR_ShadowMapCoord(D.X, D.Y, D.Z, X.X, X.Y, X.Z, Z.X, Z.Y, Z.Z,
        PsiLo, Step, Footprint, Levels));
}

WorldReliefNoise::FShadowCoord WorldReliefNoise::ShadowMapCoordF32(const FVector3f& D, const FVector3f& X, const FVector3f& Z, float PsiLo,
                                                                   float Step, float Footprint, int32 Levels)
{
    return WorldReliefLocal::ToShadowCoord(WorldReliefF32::WR_ShadowMapCoord(D.X, D.Y, D.Z, X.X, X.Y, X.Z, Z.X, Z.Y, Z.Z,
        PsiLo, Step, Footprint, Levels));
}

WorldReliefNoise::FShadowTaps WorldReliefNoise::ShadowTapsF64(double U, double V, int32 Width, int32 Rows, int32 Level)
{
    return WorldReliefLocal::ToShadowTaps(WorldReliefF64::WR_ShadowTapsAt(U, V, Width, Rows, Level));
}

WorldReliefNoise::FShadowTaps WorldReliefNoise::ShadowTapsF32(float U, float V, int32 Width, int32 Rows, int32 Level)
{
    return WorldReliefLocal::ToShadowTaps(WorldReliefF32::WR_ShadowTapsAt(U, V, Width, Rows, Level));
}

double WorldReliefNoise::BilinearF64(double A, double B, double C, double D, double FX, double FY)
{
    return WorldReliefF64::WR_Bilinear(A, B, C, D, FX, FY);
}

float WorldReliefNoise::BilinearF32(float A, float B, float C, float D, float FX, float FY)
{
    return WorldReliefF32::WR_Bilinear(A, B, C, D, FX, FY);
}

double WorldReliefNoise::LerpF64(double A, double B, double T)
{
    return WorldReliefF64::WR_Lerp(A, B, T);
}

float WorldReliefNoise::LerpF32(float A, float B, float T)
{
    return WorldReliefF32::WR_Lerp(A, B, T);
}
```

- [ ] **Step 5: The map.** Create `Source/DeepSpace/Surface/SunShadowMap.h`:

```cpp
#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Surface/SunShadow.h"

class IGroundField;

/**
 * A solid world's cast shadow for the orbit (the developer's ruling on
 * slice (b)'s build: the orbital proxy reads a per-world shadow texture
 * baked in C++ when the system loads). Pure data.
 *
 * Equirectangular in the star's own frame: FrameZ is the light
 * (SkyProjection::LightDirection), FrameX and FrameY square to it. Column c
 * is the azimuth -pi + (c + 0.5) Step about the light, row r the star's
 * elevation PsiLo + (r + 0.5) Step above the datum's level. Each texel is
 * Step square at the terminator, where the shadows are, and narrows toward
 * the point under the star, where the disc is whole. Under PsiLo no point of
 * the world, however high, sees any of the star, so the night side is not
 * stored. Levels[0] is row-major, Width x Rows, each texel
 * Quantise(visibility); level L + 1 is level L halved (BuildLevels), at the
 * GPU's own mip sizes.
 */
struct DEEPSPACE_API FSunShadowMap
{
    int32 Width = 0;
    int32 Rows = 0;
    double PsiLo = 0.0;
    double Step = 0.0;
    FVector3d FrameX = FVector3d::UnitX();
    FVector3d FrameY = FVector3d::UnitY();
    FVector3d FrameZ = FVector3d::UnitZ();
    TArray<TArray<uint16>> Levels;

    int32 WidthAt(int32 Level) const { return FMath::Max(1, Width >> Level); }
    int32 RowsAt(int32 Level) const { return FMath::Max(1, Rows >> Level); }
    int32 LevelCount() const { return Levels.Num(); }
    uint16 Texel(int32 Level, int32 X, int32 Y) const { return Levels[Level][Y * WidthAt(Level) + X]; }
    int64 Bytes() const;
};

namespace SunShadowMap
{
    /** Columns a world's map is baked with unless ds.Sky.ShadowMapWidth says
     *  otherwise: 8.8 km texels on Baemsekai IV, finer than a 4K pixel while
     *  the world is under 35 degrees across (the cast-shadow plan, Task 0). */
    inline constexpr int32 DefaultWidth = 4096;

    /** The map's frame, rows and step for Ground under Sun, and no texels;
     *  Width 0 where there is no star or no ground. */
    DEEPSPACE_API FSunShadowMap Shape(const IGroundField& Ground, const SunShadow::FSunLight& Sun, int32 Width);

    /** The unit direction, body axes, at the centre of texel (Column, Row). */
    DEEPSPACE_API FVector3d TexelDirection(const FSunShadowMap& Map, int32 Column, int32 Row);

    /** 0..1 as the 16 bits a texel holds. */
    DEEPSPACE_API uint16 Quantise(double Visible);

    /** Every texel: each column's heights read once, at the step times R as
     *  the footprint, then each row's AlongProfile over the rows ahead of it
     *  to the march's end. Rows over the day exit are whole and read nothing,
     *  and rows under the night exit are dark. Cancel, if given, is polled
     *  once a column; set, the bake stops and returns a map with no levels.
     *  Then BuildLevels. */
    DEEPSPACE_API FSunShadowMap Bake(const IGroundField& Ground, const SunShadow::FSunLight& Sun, double SteepestSlope, int32 Width,
                                     const std::atomic<bool>* Cancel = nullptr);

    /** Level L + 1 from level L until one texel is left: each texel the
     *  rounded mean of its two by two, the columns and rows past the last
     *  taken from the last, as the GPU sizes a mip (Width >> L). */
    DEEPSPACE_API void BuildLevels(FSunShadowMap& Map);

    /** What M_SkyBody and M_SkyGround read at D (body axes) for a pixel of
     *  this footprint -- one pixel's width, D units, not the faces'
     *  filter_pixels-wide footprint: the shared file's lookup and eight
     *  texel loads, trilinear, in double and in float. 0 under PsiLo, and 1
     *  for a map with no levels. */
    DEEPSPACE_API double Sample(const FSunShadowMap& Map, const FVector3d& D, double Footprint);
    DEEPSPACE_API float SampleF32(const FSunShadowMap& Map, const FVector3f& D, float Footprint);
}
```

Create `Source/DeepSpace/Surface/SunShadowMap.cpp`:

```cpp
#include "Surface/SunShadowMap.h"

#include "Surface/GroundField.h"
#include "Surface/WorldRelief.h"

int64 FSunShadowMap::Bytes() const
{
    int64 Total = 0;
    for (const TArray<uint16>& Level : Levels)
    {
        Total += static_cast<int64>(Level.Num()) * sizeof(uint16);
    }
    return Total;
}

FSunShadowMap SunShadowMap::Shape(const IGroundField& Ground, const SunShadow::FSunLight& Sun, int32 Width)
{
    FSunShadowMap Map;
    if (!Sun.IsSet() || Width < 2 || !(Ground.RadiusCm() > 0.0) || !(Ground.MaxHeightCm() > Ground.MinHeightCm()))
    {
        return Map;
    }
    Map.Width = Width;
    Map.Step = 2.0 * UE_DOUBLE_PI / Width;
    Map.FrameZ = Sun.Direction.GetSafeNormal();
    const FVector3d Seed = FMath::Abs(Map.FrameZ.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX();
    Map.FrameX = FVector3d::CrossProduct(Seed, Map.FrameZ).GetSafeNormal();
    Map.FrameY = FVector3d::CrossProduct(Map.FrameZ, Map.FrameX);
    const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunShadow::SunRadiusMax);
    Map.PsiLo = -(SunShadow::NightDip(Ground.RadiusCm(), Ground.MaxHeightCm(), Ground.MinHeightCm()) + Radius);
    Map.Rows = FMath::CeilToInt32((0.5 * UE_DOUBLE_PI - Map.PsiLo) / Map.Step);
    return Map;
}

FVector3d SunShadowMap::TexelDirection(const FSunShadowMap& Map, int32 Column, int32 Row)
{
    const double Azimuth = -UE_DOUBLE_PI + (Column + 0.5) * Map.Step;
    const double Elevation = Map.PsiLo + (Row + 0.5) * Map.Step;
    return (Map.FrameX * FMath::Cos(Azimuth) + Map.FrameY * FMath::Sin(Azimuth)) * FMath::Cos(Elevation) + Map.FrameZ * FMath::Sin(Elevation);
}

uint16 SunShadowMap::Quantise(double Visible)
{
    return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt32(Visible * 65535.0), 0, 65535));
}

FSunShadowMap SunShadowMap::Bake(const IGroundField& Ground, const SunShadow::FSunLight& Sun, double SteepestSlope, int32 Width,
                                 const std::atomic<bool>* Cancel)
{
    FSunShadowMap Map = Shape(Ground, Sun, Width);
    if (Map.Width == 0)
    {
        return Map;
    }
    const double R = Ground.RadiusCm();
    const double Peak = Ground.MaxHeightCm();
    const double Lowest = Ground.MinHeightCm();
    const double FootprintCm = Map.Step * R;
    const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunShadow::SunRadiusMax);
    const double DayExit = FMath::Atan(SteepestSlope) + Radius;
    Map.Levels.SetNum(1);
    Map.Levels[0].SetNumUninitialized(Map.Width * Map.Rows);
    TArray<double> Heights;
    Heights.SetNumUninitialized(Map.Rows);
    TBitArray<> Read;
    for (int32 Column = 0; Column < Map.Width; ++Column)
    {
        if (Cancel && Cancel->load(std::memory_order_relaxed))
        {
            return FSunShadowMap();
        }
        Read.Init(false, Map.Rows);
        const auto HeightAt = [&](int32 Row)
        {
            if (!Read[Row])
            {
                Heights[Row] = Ground.Height(TexelDirection(Map, Column, Row), FootprintCm);
                Read[Row] = true;
            }
            return Heights[Row];
        };
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            const double Psi = Map.PsiLo + (Row + 0.5) * Map.Step;
            uint16& Texel = Map.Levels[0][Row * Map.Width + Column];
            if (Psi >= DayExit)
            {
                Texel = 65535;
                continue;
            }
            const double Here = HeightAt(Row);
            if (Psi + Radius <= -SunShadow::NightDip(R, Here, Lowest))
            {
                Texel = 0;
                continue;
            }
            // The rows ahead the march can reach: every one whose arc is
            // within MarchEnd, as AlongProfile counts them.
            const double End = SunShadow::MarchEnd(R, Here, Peak, Psi - Radius);
            int32 Count = 1;
            while (Row + Count < Map.Rows && Count * Map.Step <= End)
            {
                HeightAt(Row + Count);
                ++Count;
            }
            Texel = Quantise(SunShadow::AlongProfile(R, Peak, Lowest, SteepestSlope, MakeArrayView(Heights.GetData() + Row, Count),
                Map.Step, Psi, Sun.AngularRadius).Visible);
        }
    }
    BuildLevels(Map);
    return Map;
}

void SunShadowMap::BuildLevels(FSunShadowMap& Map)
{
    if (Map.Levels.Num() == 0)
    {
        return;
    }
    Map.Levels.SetNum(1);
    for (int32 Level = 0; Map.WidthAt(Level) > 1 || Map.RowsAt(Level) > 1; ++Level)
    {
        const int32 W = Map.WidthAt(Level);
        const int32 H = Map.RowsAt(Level);
        const int32 NextW = Map.WidthAt(Level + 1);
        const int32 NextH = Map.RowsAt(Level + 1);
        TArray<uint16> Next;
        Next.SetNumUninitialized(NextW * NextH);
        const TArray<uint16>& Here = Map.Levels[Level];
        for (int32 Y = 0; Y < NextH; ++Y)
        {
            const int32 Y0 = FMath::Min(2 * Y, H - 1);
            const int32 Y1 = FMath::Min(2 * Y + 1, H - 1);
            for (int32 X = 0; X < NextW; ++X)
            {
                const int32 X0 = FMath::Min(2 * X, W - 1);
                const int32 X1 = FMath::Min(2 * X + 1, W - 1);
                const uint32 Sum = static_cast<uint32>(Here[Y0 * W + X0]) + Here[Y0 * W + X1] + Here[Y1 * W + X0] + Here[Y1 * W + X1];
                Next[Y * NextW + X] = static_cast<uint16>((Sum + 2) / 4);
            }
        }
        Map.Levels.Add(MoveTemp(Next));
    }
}

double SunShadowMap::Sample(const FSunShadowMap& Map, const FVector3d& D, double Footprint)
{
    if (Map.LevelCount() == 0)
    {
        return 1.0;
    }
    const WorldReliefNoise::FShadowCoord C = WorldReliefNoise::ShadowMapCoordF64(D, Map.FrameX, Map.FrameZ, Map.PsiLo, Map.Step, Footprint, Map.LevelCount());
    if (C.Night != 0)
    {
        return 0.0;   // under PsiLo: provably dark
    }
    double Seen[2];
    for (int32 Pick = 0; Pick < 2; ++Pick)
    {
        const int32 Level = Pick == 0 ? C.Level0 : C.Level1;
        const WorldReliefNoise::FShadowTaps T = WorldReliefNoise::ShadowTapsF64(C.U, C.V, Map.Width, Map.Rows, Level);
        const auto At = [&](int32 X, int32 Y) { return Map.Texel(Level, X, Y) / 65535.0; };
        Seen[Pick] = WorldReliefNoise::BilinearF64(At(T.X0, T.Y0), At(T.X1, T.Y0), At(T.X0, T.Y1), At(T.X1, T.Y1), T.FX, T.FY);
    }
    return WorldReliefNoise::LerpF64(Seen[0], Seen[1], C.Blend);
}

float SunShadowMap::SampleF32(const FSunShadowMap& Map, const FVector3f& D, float Footprint)
{
    if (Map.LevelCount() == 0)
    {
        return 1.0f;
    }
    const WorldReliefNoise::FShadowCoord C = WorldReliefNoise::ShadowMapCoordF32(D, FVector3f(Map.FrameX), FVector3f(Map.FrameZ),
        static_cast<float>(Map.PsiLo), static_cast<float>(Map.Step), Footprint, Map.LevelCount());
    if (C.Night != 0)
    {
        return 0.0f;
    }
    float Seen[2];
    for (int32 Pick = 0; Pick < 2; ++Pick)
    {
        const int32 Level = Pick == 0 ? C.Level0 : C.Level1;
        const WorldReliefNoise::FShadowTaps T = WorldReliefNoise::ShadowTapsF32(static_cast<float>(C.U), static_cast<float>(C.V), Map.Width, Map.Rows, Level);
        const auto At = [&](int32 X, int32 Y) { return static_cast<float>(Map.Texel(Level, X, Y)) / 65535.0f; };
        Seen[Pick] = WorldReliefNoise::BilinearF32(At(T.X0, T.Y0), At(T.X1, T.Y0), At(T.X0, T.Y1), At(T.X1, T.Y1),
            static_cast<float>(T.FX), static_cast<float>(T.FY));
    }
    return WorldReliefNoise::LerpF32(Seen[0], Seen[1], static_cast<float>(C.Blend));
}
```

`FShadowCoord` and `FShadowTaps` hold the float build's values as doubles, which represent them exactly, so casting them back to float is lossless.

- [ ] **Step 6: Run them to see them pass, and the shared file's own tests beside them.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.SunShadowMap && ./test.sh DeepSpace.Surface.WorldRelief && ./test.sh DeepSpace.Surface.ShaderMapping`

Expected: all five `.SunShadowMap.*` PASS, and every `DeepSpace.Surface.WorldRelief.*` still passes, because the file's old functions are untouched. Copy `.TexelsAreTheProfile`'s info line into the task report. `./build.sh` proves only the file's C++ half; the HLSL half is proven by Task 8's `Eyes.WorldReliefParity`, once a material includes it.

- [ ] **Step 7: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Shaders/Private/WorldRelief.ush Source/DeepSpace/Surface/WorldRelief.h Source/DeepSpace/Surface/WorldRelief.cpp \
  Source/DeepSpace/Surface/SunShadowMap.h Source/DeepSpace/Surface/SunShadowMap.cpp Source/DeepSpace/Tests/SunShadowMapTest.cpp && \
git commit -qm "feat(surface): SunShadowMap -- each solid world's cast shadow for the orbit, baked from the height function

Equirectangular in the star's own frame, texels square at the terminator,
the provably dark night side left out; each column's heights read once and
every texel's horizon over the texels ahead (SunShadow::AlongProfile);
16-bit, a level per mip at the GPU's sizes; cancellable once a column. The
lookup is the shared file's (WR_ShadowMapCoord, WR_ShadowTapsAt,
WR_Bilinear), mirrored in double and float by SunShadowMap::Sample.
DeepSpace.Surface.SunShadowMap.Shape, .TexelsAreTheProfile,
.NightBelowTheMap, .Lookup, .Mips.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Prove them.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Shaders/Private/WorldRelief.ush '    Out.U = (Phi + WR_PI) / Step - WR_REAL(0.5);' '    Out.U = (Phi + WR_PI) / Step;' 'DeepSpace.Surface.SunShadowMap.Lookup$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush '    Out.X0 = X < 0 ? X + W : (X >= W ? X - W : X);' '    Out.X0 = X < 0 ? 0 : (X >= W ? X - W : X);' 'DeepSpace.Surface.SunShadowMap.Lookup$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'const WR_REAL Across = Step * WR_max(WR_sqrt(WR_max(WR_REAL(0.0), WR_REAL(1.0) - S * S)), WR_REAL(1.0e-4));' 'const WR_REAL Across = Step;' 'DeepSpace.Surface.SunShadowMap.Lookup$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadowMap.cpp 'const double Elevation = Map.PsiLo + (Row + 0.5) * Map.Step;' 'const double Elevation = Map.PsiLo + Row * Map.Step;' 'DeepSpace.Surface.SunShadowMap.Shape$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadowMap.cpp 'Ground.MinHeightCm()) + Radius);' 'Ground.MinHeightCm()));' 'DeepSpace.Surface.SunShadowMap.Shape$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadowMap.cpp '            while (Row + Count < Map.Rows && Count * Map.Step <= End)' '            while (Row + Count < Map.Rows && Count * Map.Step <= 0.5 * End)' 'DeepSpace.Surface.SunShadowMap.TexelsAreTheProfile$'; \
Tools/mutate.sh Source/DeepSpace/Surface/SunShadowMap.cpp 'Next[Y * NextW + X] = static_cast<uint16>((Sum + 2) / 4);' 'Next[Y * NextW + X] = static_cast<uint16>(Sum / 4);' 'DeepSpace.Surface.SunShadowMap.Mips$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush '    Out.Night = Psi < PsiLo ? 1 : 0;' '    Out.Night = 0;' 'DeepSpace.Surface.SunShadowMap.Lookup$'; \
./build.sh
```

Expected: eight `KILLED`.
- The unshifted column reads half a texel off at every centre.
- The wrap that clamps at column 0 misses the seam's mean.
- The level that ignores `cos(psi)` reads 1.27 rad up at level 0, not 1.76.
- The row centred on its edge misses the elevations.
- `PsiLo` without the star's radius misses its known value.
- The profile cut to half the march's end misses the texels whose horizon lies further out: the test's own profile reads the whole column.
- The unrounded mean gives 52, not 53.
- The lookup with no night reads row 0 under `PsiLo`, not 0.

`.NightBelowTheMap` has no mutant of its own: what it proves is a consequence of the night exit's proof (Task 2) and `PsiLo`'s formula (`.Shape`), and it is there so that a later change to either cannot leave rows out that are not dark.

---

## Task 6: the materials, the contract, and the sky's bakes at system load

**Owner:** T. **Depends on:** Tasks 3, 4 and 5, and Task 0's answers.

**Files:**
- Modify: `Tools/sky_material_contract.json`. Add the parameters `shadows`, `shadow_map` (type `texture`), `shadow_frame_x` and `shadow_frame_z`. Add them to `M_SkyBody` and `M_SkyGround`, add a new material `M_SkyShadowProbe`, and add a new block `shadow`.
- Modify: `Source/DeepSpace/Sky/SkyMaterialContract.h`. Add `Shadows`, `ShadowMap`, `ShadowFrameX`, `ShadowFrameZ`, `ShadowProbePath`, `ShadowDefaultTexturePath`, `ShadowCoordEntry`, `ShadowInputs()`, `*Textures()` and `ShadowProbe*()`, and extend `BodyScalars/Vectors` and `GroundScalars/Vectors`.
- Modify: `Tools/setup_sky_materials.py`. Add `white_png`, `shadow_default_texture`, `Graph.texture`, `rgba`, `SHADOW_CODE`, `sun_shadow` and `shadow_probe`. Wire the shadow into `sky_body` and `sky_ground`, and check textures in `finish`.
- Modify: `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp`. Add texture roles and names, `CheckShadowNode`, and amend M_SkyBody's "no texture" rule.
- Modify: `Source/DeepSpace/Sky/ShipSky.h`, `Source/DeepSpace/Sky/ShipSky.cpp`. Add:
  - the CVars `ds.Sky.Shadows`, `ds.Sky.ShadowMaps`, `ds.Sky.ShadowMapWidth` and `ds.Sky.ShadowBakeTasks`;
  - the bakes, each keyed by what its map is made from and checked every frame outside transit (`SyncShadowMaps`), never tied to `RebuildFor`, at `BackgroundLow`, each cancelled and let go on its own when its key changes and at `EndPlay`;
  - landing each frame, the upload in an unbiased LOD group, the CPU copy discarded once the render thread has it, and the landing timed;
  - `DrawBodies` writing the map, the frame and the strength faded in over `ShadowFadeSeconds`, and `CopyBodyLook` copying them;
  - `ShipSky::{ShadowStrength, ShadowFadeSeconds, ShadowFade, ShadowMapWidth, FShadowKey, SameShadowKey, ShadowBakeOrder, ShadowFrameX, ShadowFrameZ, MakeShadowTexture, ShadowTextureCpuBytes, DiscardShadowTextureCpu}`.
- Create: `Source/DeepSpace/Tests/ShipSkyShadowTest.cpp` (`DeepSpace.Sky.ShadowParameters`)
- Built by the authoring run: `Content/Materials/Sky/M_SkyBody`, `M_SkyGround`, `M_SkyShadowProbe`, `T_SkyShadowWhite` (LFS)

**Interfaces:**
- Consumes: `SunShadowMap::{Bake, FSunShadowMap}` (Task 5); `SunShadow::SteepestSlope`; `SkyProjection::SunLightOf` (Task 3); `FReliefGround`; the shared file's `WR_ShadowMapCoord`, `WR_ShadowTapsAt`, `WR_Bilinear`, `WR_Lerp`; `setup_sky_materials.py`'s `custom_node`, `mask`, `probe_direction`, `ground_direction`, `body_direction`, `body_axes`.
- Produces:

```cpp
namespace SkyMaterial
{
    inline const FName Shadows, ShadowMap, ShadowFrameX, ShadowFrameZ;
    inline const TCHAR* const ShadowProbePath, ShadowDefaultTexturePath, ShadowCoordEntry;
    TArray<FName> ShadowInputs();       // Direction, Footprint, FrameX, FrameZ, ShadowMap, Vertex, Morph
    TArray<FName> BodyTextures(), GroundTextures(), ShadowProbeScalars(), ShadowProbeVectors(), ShadowProbeTextures();
}
class AShipSky
{
    void FlushShadowBakesForTest();
    UTexture2D* GetShadowTexture(FName Body) const;
    const FSunShadowMap* GetShadowMapForTest(FName Body) const;
    bool bKeepShadowMapsForTest;
    int32 GetShadowBakesPending() const;
    int32 GetShadowBakesStarted() const;
    double GetSlowestShadowLandSeconds() const;
};
namespace ShipSky
{
    struct FShadowKey { FWorldReliefParams Relief; SunShadow::FSunLight Sun; double SteepestSlope; int32 Width; };
    bool SameShadowKey(const FShadowKey& A, const FShadowKey& B);
    float ShadowStrength();
    inline constexpr double ShadowFadeSeconds = 1.0;
    float ShadowFade(double SecondsSinceLanded);
    int32 ShadowMapWidth();
    TArray<int32> ShadowBakeOrder(const FSkySystem& System, const FUniversePosition& Ship);
    FLinearColor ShadowFrameX(const FSunShadowMap& Map);   // X, and PsiLo in w
    FLinearColor ShadowFrameZ(const FSunShadowMap& Map);   // Z (the light), and Step in w
    UTexture2D* MakeShadowTexture(const FSunShadowMap& Map, FName Name);
    int64 ShadowTextureCpuBytes(const UTexture2D& Texture);
    void DiscardShadowTextureCpu(UTexture2D& Texture);
}
```

- [ ] **Step 1: Write the failing test.** Create `Source/DeepSpace/Tests/ShipSkyShadowTest.cpp`:

```cpp
#include "Components/StaticMeshComponent.h"
#include "Engine/Texture2D.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Surface/SunShadowMap.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowParametersTest, "DeepSpace.Sky.ShadowParameters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The sky bakes each solid world's cast-shadow map and hands it to the
 * world's material: a 16-bit texture, a mip per level, never LOD-biased, its
 * CPU copy discarded once uploaded, its frame the sky's own light
 * (SkyProjection::SunLightOf) and ds.Sky.Shadows clamped to 0..1 as the
 * strength; the ground's instance gets all of it through CopyBodyLook.
 * Worlds with no ground get no map. Each map is keyed by what it is made
 * from, so a jump within the system (a new jump serial, the same system)
 * re-bakes nothing, a reload that moves one world's relief re-bakes that
 * world alone, a new system drops the old maps, and nothing waits for a
 * bake it lets go. A map that lands fades in over ShadowFadeSeconds. At 256
 * columns, so the test bakes in a moment.
 */
bool FShadowParametersTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FScopedCVar Narrow(TEXT("ds.Sky.ShadowMapWidth"), 256.0f);
    FSkyWorld Test(TEXT("ShadowParametersWorld"), 8, EShadows::Maps);
    Test.Sky->bKeepShadowMapsForTest = true;
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    TestTrue(TEXT("the bakes start when the system loads"), Test.Sky->GetShadowBakesPending() > 0);
    Test.Sky->FlushShadowBakesForTest();
    Test.Step(1.0f / 60.0f);
    TestEqual(TEXT("and all land"), Test.Sky->GetShadowBakesPending(), 0);

    const FSkySystem Here = LocalSystem::Here(Test.World);
    const int32 Serial = LocalSystem::Serial(Test.World);
    const TArray<int32> Order = ShipSky::ShadowBakeOrder(Here, Test.Ship->GetFlightState().GetUniversePosition());
    int32 Solid = 0;
    for (int32 Index = 0; Index < Here.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = Here.Bodies[Index];
        const bool bSolid = Body.Ground == EGround::Solid && Body.Kind != ESkyBodyKind::Star;
        Solid += bSolid ? 1 : 0;
        TestEqual(FString::Printf(TEXT("%s is baked exactly when it has ground"), *Body.Id.ToString()), Order.Contains(Index), bSolid);
        UTexture2D* Texture = Test.Sky->GetShadowTexture(Body.Id);
        if (!bSolid)
        {
            TestNull(FString::Printf(TEXT("%s has no map"), *Body.Id.ToString()), Texture);
            continue;
        }
        const FSunShadowMap* Map = Test.Sky->GetShadowMapForTest(Body.Id);
        if (!TestNotNull(FString::Printf(TEXT("%s has its map"), *Body.Id.ToString()), Texture) || !TestNotNull(TEXT("and kept it"), Map))
        {
            continue;
        }
        TestTrue(FString::Printf(TEXT("%s: the texture is the map, 256 x %d, G16, a mip per level"), *Body.Id.ToString(), Map->Rows),
            Texture->GetSizeX() == 256 && Texture->GetSizeY() == Map->Rows && Texture->GetPixelFormat() == PF_G16
            && Texture->GetNumMips() == Map->LevelCount());
        // A device profile's LOD bias would drop mip 0 while the lookup's U
        // is still in level-0 texels: every read in the wrong place.
        TestEqual(TEXT("in an LOD group no profile biases"), static_cast<int32>(Texture->LODGroup), static_cast<int32>(TEXTUREGROUP_Pixels2D));
        TestEqual(TEXT("with no copy left on the CPU once uploaded"), ShipSky::ShadowTextureCpuBytes(*Texture), static_cast<int64>(0));
        TestTrue(TEXT("baked under the sky's own light"), Map->FrameZ.Equals(SkyProjection::SunLightOf(Here, Index).Direction.GetSafeNormal(), 1e-12));
        UMaterialInstanceDynamic* Instance = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(Index)->GetMaterial(0));
        if (!TestNotNull(TEXT("the proxy draws a dynamic instance"), Instance))
        {
            continue;
        }
        TestTrue(TEXT("which reads that texture"), Instance->K2_GetTextureParameterValue(SkyMaterial::ShadowMap) == Texture);
        TestTrue(TEXT("in the map's frame, PsiLo and Step in w"),
            Instance->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameX).Equals(ShipSky::ShadowFrameX(*Map), 0.0f)
            && Instance->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameZ).Equals(ShipSky::ShadowFrameZ(*Map), 0.0f));
    }
    TestTrue(TEXT("the start system has solid worlds"), Solid > 0);
    for (int32 Rank = 1; Rank < Order.Num(); ++Rank)
    {
        const FUniversePosition Ship = Test.Ship->GetFlightState().GetUniversePosition();
        const FSkyBody& Nearer = Here.Bodies[Order[Rank - 1]];
        const FSkyBody& Farther = Here.Bodies[Order[Rank]];
        TestTrue(TEXT("the bakes run nearest first"),
            Ship.DistanceTo(Nearer.Position) - Nearer.Radius <= Ship.DistanceTo(Farther.Position) - Farther.Radius);
    }

    // The strength: ds.Sky.Shadows, clamped (a flush lands its maps faded in).
    UMaterialInstanceDynamic* Fourth = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(4)->GetMaterial(0));
    for (const TPair<float, float>& Case : { TPair<float, float>(7.0f, 1.0f), TPair<float, float>(-1.0f, 0.0f), TPair<float, float>(0.5f, 0.5f) })
    {
        FScopedCVar Strength(TEXT("ds.Sky.Shadows"), Case.Key);
        Test.Step(1.0f / 60.0f);
        TestEqual(FString::Printf(TEXT("ds.Sky.Shadows %.1f draws %.1f"), Case.Key, Case.Value), ShipSky::ShadowStrength(), Case.Value);
        TestEqual(TEXT("and the world's material has it"), Fourth->K2_GetScalarParameterValue(SkyMaterial::Shadows), Case.Value);
    }

    // The ground gets all of it through the look's one copy.
    UMaterial* GroundMaterial = LoadObject<UMaterial>(nullptr, SkyMaterial::GroundPath);
    if (TestNotNull(TEXT("M_SkyGround is built"), GroundMaterial))
    {
        UMaterialInstanceDynamic* Ground = UMaterialInstanceDynamic::Create(GroundMaterial, Test.World);
        ShipSky::CopyBodyLook(*Fourth, *Ground);
        TestTrue(TEXT("the ground reads the world's map"),
            Ground->K2_GetTextureParameterValue(SkyMaterial::ShadowMap) == Fourth->K2_GetTextureParameterValue(SkyMaterial::ShadowMap));
        TestTrue(TEXT("in its frame"), Ground->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameX).Equals(Fourth->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameX), 0.0f)
            && Ground->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameZ).Equals(Fourth->K2_GetVectorParameterValue(SkyMaterial::ShadowFrameZ), 0.0f));
        TestEqual(TEXT("at its strength"), Ground->K2_GetScalarParameterValue(SkyMaterial::Shadows), Fourth->K2_GetScalarParameterValue(SkyMaterial::Shadows));
    }

    // A jump within the system: ShipNavState bumps the jump serial on an
    // in-system arrival, and the sky rebuilds its proxies for it. Neither of a
    // map's inputs moved, so nothing is baked again and every texture stays.
    const FName Nearest = Here.Bodies[Order[0]].Id;
    TMap<FName, UTexture2D*> Held;
    for (const int32 Index : Order)
    {
        Held.Add(Here.Bodies[Index].Id, Test.Sky->GetShadowTexture(Here.Bodies[Index].Id));
    }
    const int32 Started = Test.Sky->GetShadowBakesStarted();
    Test.Sky->RebuildFor(Here);
    Test.Sky->SyncTo(Here, Serial + 1, false);
    Test.Step(1.0f / 60.0f);
    TestEqual(TEXT("a jump within the system re-bakes nothing"), Test.Sky->GetShadowBakesStarted(), Started);
    TestEqual(TEXT("and leaves nothing pending"), Test.Sky->GetShadowBakesPending(), 0);
    bool bSame = true;
    for (const TPair<FName, UTexture2D*>& Was : Held)
    {
        bSame = bSame && Test.Sky->GetShadowTexture(Was.Key) == Was.Value;
    }
    TestTrue(TEXT("and every world keeps its texture"), bSame);

    // In transit the maps are left as they are, whatever the system reads.
    // (The step above rebuilt for the real serial again, so Serial is what is built.)
    Test.Sky->SyncTo(FSkySystem(), Serial, true);
    TestTrue(TEXT("in transit the maps are kept"), Test.Sky->GetShadowTexture(Nearest) == Held[Nearest]);

    // A reload of the priors that moves one world's relief keeps the serial:
    // that world alone is baked again, and its old map is let go at once.
    {
        FSkySystem Reloaded = Here;
        Reloaded.Bodies[Order[0]].Relief.PeakCm *= 1.1;
        Test.Sky->SyncTo(Reloaded, Serial, false);
        TestEqual(TEXT("a reload that moves one world's relief re-bakes that world alone"), Test.Sky->GetShadowBakesStarted(), Started + 1);
        TestNull(TEXT("and lets its stale map go"), Test.Sky->GetShadowTexture(Nearest));
        bool bOthers = true;
        for (const TPair<FName, UTexture2D*>& Was : Held)
        {
            bOthers = bOthers && (Was.Key == Nearest || Test.Sky->GetShadowTexture(Was.Key) == Was.Value);
        }
        TestTrue(TEXT("and every other world keeps its own"), bOthers);
    }

    // A map that lands fades in: a new width re-bakes every map, and the
    // frame the nearest one lands in draws it at strength 0, a second later
    // whole. (The next frame reads the real system again, so the reloaded
    // world is baked back to its own relief with the rest.)
    {
        FScopedCVar Wider(TEXT("ds.Sky.ShadowMapWidth"), 512.0f);
        UTexture2D* Landed = nullptr;
        for (int32 Tries = 0; Tries < 600 && !Landed; ++Tries)
        {
            Test.Step(1.0f / 60.0f);
            FPlatformProcess::Sleep(0.005f);
            Landed = Test.Sky->GetShadowTexture(Nearest);
        }
        if (TestNotNull(TEXT("the nearest world's map lands at the new width"), Landed))
        {
            TestEqual(TEXT("at 512 columns"), Landed->GetSizeX(), 512);
            UMaterialInstanceDynamic* Near = Cast<UMaterialInstanceDynamic>(Test.Sky->GetProxy(Order[0])->GetMaterial(0));
            TestEqual(TEXT("and the frame it lands in draws it at strength 0: no shadow appears in one frame"),
                Near->K2_GetScalarParameterValue(SkyMaterial::Shadows), 0.0f);
            for (int32 Tick = 0; Tick < FMath::CeilToInt32(ShipSky::ShadowFadeSeconds * 60.0f) + 2; ++Tick)
            {
                Test.Step(1.0f / 60.0f);
            }
            TestEqual(TEXT("and one fade later, whole"), Near->K2_GetScalarParameterValue(SkyMaterial::Shadows), ShipSky::ShadowStrength());
        }
        TestEqual(TEXT("the fade is 0 as a map lands"), ShipSky::ShadowFade(0.0), 0.0f);
        TestEqual(TEXT("half at half the fade"), ShipSky::ShadowFade(0.5 * ShipSky::ShadowFadeSeconds), 0.5f);
        TestEqual(TEXT("and whole after it"), ShipSky::ShadowFade(7.0), 1.0f);
        Test.Sky->FlushShadowBakesForTest();
    }

    // A new system: the old maps go, and the bakes they had are let go.
    Test.Sky->SyncTo(FSkySystem(), Serial + 2, false);
    TestNull(TEXT("a new system drops the old maps"), Test.Sky->GetShadowTexture(Nearest));
    TestEqual(TEXT("and has nothing pending"), Test.Sky->GetShadowBakesPending(), 0);
    Test.Step(1.0f / 60.0f);   // back to the start system: it queues again
    TestTrue(TEXT("and coming back queues its worlds again"), Test.Sky->GetShadowBakesPending() > 0);
    {
        FScopedCVar None(TEXT("ds.Sky.ShadowMaps"), 0.0f);
        Test.Step(1.0f / 60.0f);
        TestEqual(TEXT("with ds.Sky.ShadowMaps 0 nothing is baked, and what was baking is let go"), Test.Sky->GetShadowBakesPending(), 0);
        TestNull(TEXT("and no map is held"), Test.Sky->GetShadowTexture(Nearest));
    }
    return true;
}

#endif
```

- [ ] **Step 2: Run it to see it fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails, because `SkyMaterial::ShadowMap`, `AShipSky::FlushShadowBakesForTest`, `ShipSky::ShadowStrength` and the rest are undeclared.

- [ ] **Step 3: The contract's header.** In `Source/DeepSpace/Sky/SkyMaterialContract.h`, after `inline const FName BodyAxisY = ...;`, add:

```cpp
    // The cast shadow (the developer's ruling on slice (b)'s build: baked,
    // not marched). M_SkyBody reads the world's map, M_SkyGround the same map
    // blended into its vertices' shadow by Morph, both through one Custom node
    // over the shared file's WR_ShadowMapCoord (SunShadowMap::Sample is its
    // C++ mirror). The strength is ds.Sky.Shadows, lerp(1, shadow, Shadows).
    inline const FName Shadows = TEXT("Shadows");           // scalar: the cast shadow's strength, 0..1
    inline const FName ShadowMap = TEXT("ShadowMap");       // texture: the world's map, G16, a mip per level (SunShadowMap)
    inline const FName ShadowFrameX = TEXT("ShadowFrameX"); // vector: the map's X axis, body axes; w its PsiLo, rad
    inline const FName ShadowFrameZ = TEXT("ShadowFrameZ"); // vector: its Z axis, the light, body axes; w its Step, rad
    inline const TCHAR* const ShadowProbePath = TEXT("/Game/Materials/Sky/M_SkyShadowProbe.M_SkyShadowProbe");
    /** What a world without a baked map reads: a white 16-bit texture, so the
     *  lookup is 1, no shadow. Authored by setup_sky_materials.py. */
    inline const TCHAR* const ShadowDefaultTexturePath = TEXT("/Game/Materials/Sky/T_SkyShadowWhite.T_SkyShadowWhite");
    /** The shadow node's include is WorldReliefInclude; it calls this, and takes these pins, in order. */
    inline const TCHAR* const ShadowCoordEntry = TEXT("WR_ShadowMapCoord");
    inline TArray<FName> ShadowInputs() { return { TEXT("Direction"), TEXT("Footprint"), TEXT("FrameX"), TEXT("FrameZ"), TEXT("ShadowMap"), TEXT("Vertex"), TEXT("Morph") }; }
```

Replace the parameter lists with:

```cpp
    inline TArray<FName> BodyScalars() { return { Brightness, PointBlend, Mottle, Detail, Banding, ReliefScale, Cratering, Shadows }; }
    inline TArray<FName> BodyVectors() { return { Colour, LightDirection, Rim, SurfaceSeed, BodyAxisX, BodyAxisY, ShadowFrameX, ShadowFrameZ }; }
    inline TArray<FName> BodyTextures() { return { ShadowMap }; }
    inline TArray<FName> StarScalars() { return { Brightness }; }
    inline TArray<FName> StarVectors() { return { Colour }; }
    inline TArray<FName> ParameterScalars() { return { InteriorLight, Veil }; }
    inline TArray<FName> ProbeScalars() { return { Banding, ProbeFootprint }; }
    inline TArray<FName> ProbeVectors() { return { SurfaceSeed, ProbeSelect, ProbeBias }; }
    inline TArray<FName> GroundScalars() { return { Brightness, Mottle, Detail, Cratering, ReliefScale, Morph, BandLimit, Shadows }; }
    inline TArray<FName> GroundVectors() { return { Colour, LightDirection, SurfaceSeed, TilePivot, ShadowFrameX, ShadowFrameZ }; }
    inline TArray<FName> GroundTextures() { return { ShadowMap }; }
    inline TArray<FName> GroundProbeScalars() { return { Cratering, ReliefScale, VertexBandLimit, ProbeFootprint }; }
    inline TArray<FName> GroundProbeVectors() { return { SurfaceSeed, ProbeBias }; }
    inline TArray<FName> ShadowProbeScalars() { return { ProbeFootprint }; }
    inline TArray<FName> ShadowProbeVectors() { return { ShadowFrameX, ShadowFrameZ, ProbeBias }; }
    inline TArray<FName> ShadowProbeTextures() { return { ShadowMap }; }
```

Also amend the header comment's M_SkyBody line to list `Shadows`, `ShadowFrameX` and `ShadowFrameZ`, and the texture `ShadowMap`.

- [ ] **Step 4: The contract's JSON.** In `Tools/sky_material_contract.json`, add to `"parameters"`:

```json
    "shadows":           { "name": "Shadows",         "type": "scalar" },
    "shadow_map":        { "name": "ShadowMap",       "type": "texture" },
    "shadow_frame_x":    { "name": "ShadowFrameX",    "type": "vector" },
    "shadow_frame_z":    { "name": "ShadowFrameZ",    "type": "vector" }
```

In `"materials"`, append `"shadows", "shadow_map", "shadow_frame_x", "shadow_frame_z"` to `M_SkyBody`'s and `M_SkyGround`'s lists, and add:

```json
    "M_SkyShadowProbe": { "parameters": ["shadow_map", "shadow_frame_x", "shadow_frame_z", "probe_footprint", "probe_bias"] }
```

After `"shared_relief"`, add:

```json
  "shadow": {
    "comment": "The cast shadow (baked, not marched): M_SkyBody, M_SkyGround and M_SkyShadowProbe each call the world's map through one Custom node over the shared file -- WR_ShadowMapCoord for the texel coordinates and the level (and Night, under PsiLo, where the map is 0), WR_ShadowTapsAt for the four texels at each of two levels, WR_Bilinear and WR_Lerp to blend them, eight Loads of ShadowMap -- and then lerp(map, Vertex, Morph): the body passes Vertex 1 and Morph 0, the ground its UV0.x and its Morph. Footprint is one pixel's: the body and the ground pass their face's footprint over filter_pixels, the probe ProbeFootprint as it is. SkyMaterialContract.h holds the same entry and pins; DeepSpace.Sky.MaterialContract holds the built nodes to them. default_texture is what a world without a map reads, white.",
    "entry": "WR_ShadowMapCoord",
    "inputs": ["Direction", "Footprint", "FrameX", "FrameZ", "ShadowMap", "Vertex", "Morph"],
    "default_texture": "/Game/Materials/Sky/T_SkyShadowWhite.T_SkyShadowWhite"
  },
```

- [ ] **Step 5: The contract's test.** In `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp`:

(a) In `Roles()`, append:

```cpp
            { TEXT("shadows"), SkyMaterial::Shadows, TEXT("scalar") },
            { TEXT("shadow_map"), SkyMaterial::ShadowMap, TEXT("texture") },
            { TEXT("shadow_frame_x"), SkyMaterial::ShadowFrameX, TEXT("vector") },
            { TEXT("shadow_frame_z"), SkyMaterial::ShadowFrameZ, TEXT("vector") },
```

(b) `JsonNames` gains a fourth parameter, `TSet<FName>& OutTextures`, and its sorting line becomes:

```cpp
            const FString Type = Parameter->GetStringField(TEXT("type"));
            (Type == TEXT("scalar") ? OutScalars : (Type == TEXT("texture") ? OutTextures : OutVectors)).Add(Name);
```

It has two callers. In the per-material loop, replace

```cpp
        TSet<FName> JsonScalars;
        TSet<FName> JsonVectors;
        JsonNames(Contract, *Entry, JsonScalars, JsonVectors);
```

with

```cpp
        TSet<FName> JsonScalars;
        TSet<FName> JsonVectors;
        TSet<FName> JsonTextures;
        JsonNames(Contract, *Entry, JsonScalars, JsonVectors, JsonTextures);
```

In the `MPC_Sky` block, replace `JsonNames(Contract, Collection, JsonScalars, JsonVectors);` with

```cpp
        TSet<FName> JsonTextures;
        JsonNames(Contract, Collection, JsonScalars, JsonVectors, JsonTextures);
        TestEqual(TEXT("MPC_Sky has no textures"), JsonTextures.Num(), 0);
```

(c) `AssetNames` takes the type:

```cpp
    TSet<FName> AssetNames(const UMaterialInterface* Material, EMaterialParameterType Type)
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        switch (Type)
        {
        case EMaterialParameterType::Scalar: Material->GetAllScalarParameterInfo(Infos, Ids); break;
        case EMaterialParameterType::Vector: Material->GetAllVectorParameterInfo(Infos, Ids); break;
        default: Material->GetAllTextureParameterInfo(Infos, Ids); break;
        }
        TSet<FName> Names;
        for (const FMaterialParameterInfo& Info : Infos)
        {
            Names.Add(Info.Name);
        }
        return Names;
    }
```

The two existing calls become `AssetNames(Material, EMaterialParameterType::Scalar)` and `AssetNames(Material, EMaterialParameterType::Vector)`.

(d) `FExpected` gains `TArray<FName> Textures` after `Vectors`. The table becomes:

```cpp
        { TEXT("M_SkyBody"), SkyMaterial::BodyPath, SkyMaterial::BodyScalars(), SkyMaterial::BodyVectors(), SkyMaterial::BodyTextures() },
        { TEXT("M_SkyStar"), SkyMaterial::StarPath, SkyMaterial::StarScalars(), SkyMaterial::StarVectors(), {} },
        { TEXT("M_SkyStarfield"), SkyMaterial::StarfieldPath, {}, {}, {} },
        { TEXT("M_SkyGlass"), SkyMaterial::GlassPath, {}, {}, {} },
        { TEXT("M_SkyReliefProbe"), SkyMaterial::ReliefProbePath, SkyMaterial::ProbeScalars(), SkyMaterial::ProbeVectors(), {} },
        { TEXT("M_SkyGround"), SkyMaterial::GroundPath, SkyMaterial::GroundScalars(), SkyMaterial::GroundVectors(), SkyMaterial::GroundTextures() },
        { TEXT("M_SkyGroundProbe"), SkyMaterial::GroundProbePath, SkyMaterial::GroundProbeScalars(), SkyMaterial::GroundProbeVectors(), {} },
        { TEXT("M_SkyShadowProbe"), SkyMaterial::ShadowProbePath, SkyMaterial::ShadowProbeScalars(), SkyMaterial::ShadowProbeVectors(), SkyMaterial::ShadowProbeTextures() },
```

(e) After the vectors' two checks in the loop, add the textures':

```cpp
        TestTrue(FString::Printf(TEXT("%s: the JSON's textures %s are the header's"), Expected.Asset, *Describe(JsonTextures)),
            SameSet(JsonTextures, TSet<FName>(Expected.Textures)));
```

After the asset's vectors check, add:

```cpp
        const TSet<FName> Textures = AssetNames(Material, EMaterialParameterType::Texture);
        TestTrue(FString::Printf(TEXT("%s exposes exactly the contract's textures; has %s"), Expected.Asset, *Describe(Textures)),
            SameSet(Textures, JsonTextures));
        int32 TextureNodes = 0;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
        {
            TextureNodes += Cast<UMaterialExpressionTextureObjectParameter>(Expression) ? 1 : 0;
        }
        TestEqual(FString::Printf(TEXT("%s has one node per texture"), Expected.Asset), TextureNodes, JsonTextures.Num());
```

(f) In `CheckSurfaceFace`, replace

```cpp
            Test.TestFalse(FString::Printf(TEXT("M_SkyBody samples no texture (%s)"), *Node->GetName()),
                Cast<UMaterialExpressionTextureBase>(Node) != nullptr);
```

with

```cpp
            // The one texture M_SkyBody reads is its cast-shadow map, as an
            // object, and only the shadow's node takes it (CheckShadowNode);
            // nothing samples one into the face.
            const UMaterialExpressionTextureObjectParameter* Object = Cast<UMaterialExpressionTextureObjectParameter>(Node);
            Test.TestTrue(FString::Printf(TEXT("M_SkyBody reads no texture but its shadow map (%s)"), *Node->GetName()),
                Cast<UMaterialExpressionTextureBase>(Node) == nullptr || (Object && Object->ParameterName == SkyMaterial::ShadowMap));
```

(g) Add, before `RunTest`:

```cpp
    /**
     * The cast shadow's node, in a material that reads the map: exactly one
     * Custom node calls WR_ShadowMapCoord, through the shared file's include,
     * with the contract's pins in order; it alone takes the ShadowMap object;
     * the object's default is the white texture, so a world without a map
     * reads 1; and a face hands it one pixel's footprint.
     */
    void CheckShadowNode(FAutomationTestBase& Test, UMaterial& Material, const TSharedPtr<FJsonObject>& Contract)
    {
        const TSharedPtr<FJsonObject> Block = Contract->GetObjectField(TEXT("shadow"));
        Test.TestEqual(TEXT("the JSON's shadow entry is the header's"), Block->GetStringField(TEXT("entry")), FString(SkyMaterial::ShadowCoordEntry));
        Test.TestEqual(TEXT("and its default texture"), Block->GetStringField(TEXT("default_texture")), FString(SkyMaterial::ShadowDefaultTexturePath));
        TArray<FName> JsonInputs;
        for (const TSharedPtr<FJsonValue>& Value : Block->GetArrayField(TEXT("inputs")))
        {
            JsonInputs.Add(FName(*Value->AsString()));
        }
        Test.TestTrue(TEXT("and its pins"), JsonInputs == SkyMaterial::ShadowInputs());
        int32 Nodes = 0;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            if (const UMaterialExpressionTextureObjectParameter* Object = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
            {
                Test.TestTrue(FString::Printf(TEXT("%s: the map's default is the white texture"), *Material.GetName()),
                    Object->Texture && Object->Texture->GetPathName() == SkyMaterial::ShadowDefaultTexturePath);
            }
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (!Custom)
            {
                continue;
            }
            bool bTakesMap = false;
            for (const FCustomInput& Input : Custom->Inputs)
            {
                bTakesMap = bTakesMap || Cast<UMaterialExpressionTextureObjectParameter>(Input.Input.Expression) != nullptr;
            }
            if (!Custom->Code.Contains(SkyMaterial::ShadowCoordEntry))
            {
                Test.TestFalse(FString::Printf(TEXT("%s: only the shadow's node takes the map (%s)"), *Material.GetName(), *Custom->GetName()), bTakesMap);
                continue;
            }
            ++Nodes;
            TArray<FName> Pins;
            for (const FCustomInput& Input : Custom->Inputs)
            {
                Pins.Add(Input.InputName);
            }
            Test.TestTrue(FString::Printf(TEXT("%s: the shadow's pins are the contract's"), *Material.GetName()), Pins == SkyMaterial::ShadowInputs());
            Test.TestTrue(FString::Printf(TEXT("%s: through the shared file"), *Material.GetName()),
                Custom->IncludeFilePaths.Contains(FString(SkyMaterial::WorldReliefInclude)));
            Test.TestTrue(FString::Printf(TEXT("%s: and it takes the map"), *Material.GetName()), bTakesMap);
            // The faces hand the node one pixel's footprint: theirs over
            // filter_pixels (face_footprint). Handed the filtered one, the
            // map would be read a level coarser than planned everywhere.
            if (Material.GetFName() != TEXT("M_SkyShadowProbe") && Custom->Inputs.Num() > 1)
            {
                const UMaterialExpressionMultiply* Over = Cast<UMaterialExpressionMultiply>(Custom->Inputs[1].Input.Expression);
                const UMaterialExpressionConstant* By = Over ? Cast<UMaterialExpressionConstant>(Over->B.Expression) : nullptr;
                const double FilterPixels = Contract->GetObjectField(TEXT("constants"))->GetNumberField(TEXT("filter_pixels"));
                Test.TestTrue(FString::Printf(TEXT("%s: the shadow's footprint is the face's over filter_pixels"), *Material.GetName()),
                    By && FMath::IsNearlyEqual(static_cast<double>(By->R), 1.0 / FilterPixels, 1e-6));
            }
        }
        Test.TestEqual(FString::Printf(TEXT("%s has one shadow node"), *Material.GetName()), Nodes, 1);
    }
```

In `RunTest`'s per-material checks, add:

```cpp
        if (Material->GetFName() == TEXT("M_SkyBody") || Material->GetFName() == TEXT("M_SkyGround") || Material->GetFName() == TEXT("M_SkyShadowProbe"))
        {
            CheckShadowNode(*this, *const_cast<UMaterial*>(Material), Contract);
        }
```

(h) `CheckSharedRelief` holds a material to *one* Custom node, and M_SkyBody now has two: the face's and the shadow's. The shadow's node includes the same file for its map's lookup, and `CheckShadowNode` holds it. So in `CheckSharedRelief`'s loop, replace

```cpp
            if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression.Get()))
            {
                Customs.Add(Custom);
            }
```

with

```cpp
            // The cast shadow's node includes the same file for its map's
            // lookup (CheckShadowNode holds it); the face's is the other.
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression.Get());
            if (Custom && !Custom->Code.Contains(SkyMaterial::ShadowCoordEntry))
            {
                Customs.Add(Custom);
            }
```

`CheckSurfaceFace`'s structural checks are unchanged. They are read from the face's node, which the shadow's does not feed, and the shadow multiplies `shaded` after the light's dot products, so the guard, the unit normals and the knobs' paths are as they were. The new parameters (`Shadows`, `ShadowFrameX`, `ShadowFrameZ`) reach the pixel, which its last loop checks.

Add `#include "Materials/MaterialExpressionTextureObjectParameter.h"`, `#include "Materials/MaterialExpressionMultiply.h"` and `#include "Materials/MaterialExpressionConstant.h"` with the other material includes (skip any already there).

- [ ] **Step 6: The authoring.** In `Tools/setup_sky_materials.py`:

(a) After `SHARED = CONTRACT["shared_relief"]`, add `SHADOW = CONTRACT["shadow"]`. After `SHARED_CODE`, add:

```python
# The cast shadow's Custom node (the developer's ruling on slice (b)'s build:
# baked, not marched): the world's map read at D -- the shared file's
# coordinates, taps and blends, eight texel loads, trilinear -- then lerp(map,
# Vertex, Morph). The body passes Vertex 1 and Morph 0; the ground its UV0.x
# and its Morph, and skips the loads once the relief has grown in. Under PsiLo
# the map is 0 (C.Night: provably dark). Footprint is one pixel's width: the
# faces hand it their filtered footprint over filter_pixels (face_footprint).
# HLSL only: the arithmetic is the shared file's, and SunShadowMap::Sample
# mirrors it.
SHADOW_CODE = (
    "if (Morph >= 1.0) { return Vertex; }\n"
    "uint MapWidth; uint MapRows; uint MapLevels;\n"
    "ShadowMap.GetDimensions(0, MapWidth, MapRows, MapLevels);\n"
    "WR_ShadowCoord C = %s(Direction.x, Direction.y, Direction.z, FrameX.x, FrameX.y, FrameX.z, FrameZ.x, FrameZ.y, FrameZ.z, "
    "FrameX.w, FrameZ.w, Footprint, int(MapLevels));\n"
    "if (C.Night != 0) { return WR_Lerp(0.0, Vertex, Morph); }\n"
    "WR_ShadowTaps T0 = WR_ShadowTapsAt(C.U, C.V, int(MapWidth), int(MapRows), C.Level0);\n"
    "WR_ShadowTaps T1 = WR_ShadowTapsAt(C.U, C.V, int(MapWidth), int(MapRows), C.Level1);\n"
    "float Seen0 = WR_Bilinear(ShadowMap.Load(int3(T0.X0, T0.Y0, C.Level0)).r, ShadowMap.Load(int3(T0.X1, T0.Y0, C.Level0)).r,\n"
    "                          ShadowMap.Load(int3(T0.X0, T0.Y1, C.Level0)).r, ShadowMap.Load(int3(T0.X1, T0.Y1, C.Level0)).r, T0.FX, T0.FY);\n"
    "float Seen1 = WR_Bilinear(ShadowMap.Load(int3(T1.X0, T1.Y0, C.Level1)).r, ShadowMap.Load(int3(T1.X1, T1.Y0, C.Level1)).r,\n"
    "                          ShadowMap.Load(int3(T1.X0, T1.Y1, C.Level1)).r, ShadowMap.Load(int3(T1.X1, T1.Y1, C.Level1)).r, T1.FX, T1.FY);\n"
    "return WR_Lerp(WR_Lerp(Seen0, Seen1, C.Blend), Vertex, Morph);\n" % SHADOW["entry"])
```

(b) In `class Graph`, after `vector`, add:

```python
    def texture(self, role, default):
        """A texture object parameter: handed to a Custom node whole, which
        loads its texels itself. Grayscale, as the map and its default are."""
        expression = self.node(unreal.MaterialExpressionTextureObjectParameter,
                               parameter_name=name(role), texture=default,
                               sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_GRAYSCALE)
        self.parameters[role] = expression
        return expression
```

(c) Before `# -- the materials ---`, add:

```python
def white_png(path, size=4):
    """A size x size 16-bit grayscale PNG, every sample 65535: the map a world
    without one reads, so the shadow's lookup is 1."""
    import struct
    import zlib
    raw = b"".join(b"\x00" + b"\xff\xff" * size for _ in range(size))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 16, 0, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def shadow_default_texture():
    """T_SkyShadowWhite: imported from white_png, linear, grayscale (G16), no
    mips -- what the shadow map parameters default to."""
    path = os.path.join(unreal.Paths.project_saved_dir(), "T_SkyShadowWhite.png")
    white_png(path)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", path)
    task.set_editor_property("destination_path", DIRECTORY)
    task.set_editor_property("destination_name", "T_SkyShadowWhite")
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.EditorAssetLibrary.load_asset(SHADOW["default_texture"])
    if texture is None:
        raise RuntimeError("T_SkyShadowWhite did not import")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    log("T_SkyShadowWhite: %s" % texture.get_path_name())
    return texture


def face_footprint(g, footprint):
    """One pixel's footprint from a face's filtered one: the faces' is
    max(|ddx D|, |ddy D|) * filter_pixels, and the shadow's lookup takes a
    pixel's (WR_ShadowMapCoord), so level 0 is read wherever a texel covers
    a pixel -- not only where it covers filter_pixels of them."""
    return g.mul(footprint, g.constant(1.0 / CONSTANTS["filter_pixels"]))


def rgba(g, vector):
    """A vector parameter's four channels: its default output is only the three."""
    return g.binary(unreal.MaterialExpressionAppendVector, mask(g, vector, "rgb"), mask(g, vector, "r", output_name="A"))


def sun_shadow(g, direction, footprint, default_texture, vertex, morph):
    """The cast shadow at D (body axes) for this footprint: the world's map,
    blended into the vertex's own by morph (SHADOW_CODE). Makes the map's
    three parameters -- a material has one shadow node -- and returns the
    node, its pins in the contract's order."""
    frame_x = g.vector("shadow_frame_x", (1.0, 0.0, 0.0, -1.5707963))
    frame_z = g.vector("shadow_frame_z", (0.0, 0.0, 1.0, 1.0))
    shadow_map = g.texture("shadow_map", default_texture)
    pins = [("Direction", direction), ("Footprint", footprint), ("FrameX", rgba(g, frame_x)),
            ("FrameZ", rgba(g, frame_z)), ("ShadowMap", shadow_map), ("Vertex", vertex), ("Morph", morph)]
    if [pin for pin, _ in pins] != SHADOW["inputs"]:
        raise RuntimeError("the shadow node's pins are not the contract's: %s" % SHADOW["inputs"])
    return custom_node(g, SHADOW_CODE, pins, unreal.CustomMaterialOutputType.CMOT_FLOAT1)


def cast(g, shaded, shadow, strength):
    """shaded x lerp(1, shadow, Shadows): 0 draws the unshadowed look."""
    blend = g.node(unreal.MaterialExpressionLinearInterpolate, const_a=1.0)
    g.link(shadow, blend, "B")
    g.link(strength, blend, "Alpha")
    return g.mul(shaded, blend)
```

(d) `sky_body()` and `sky_ground()` gain a `default_texture` argument: change `def sky_body():` to `def sky_body(default_texture):` and `def sky_ground():` to `def sky_ground(default_texture):`. In `sky_body`, replace

```python
    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    disc = g.mul(shaded, factor)
```

with

```python
    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    shadow = sun_shadow(g, direction, face_footprint(g, footprint), default_texture, g.constant(1.0), g.constant(0.0))
    disc = g.mul(cast(g, shaded, shadow, g.scalar("shadows", 1.0)), factor)
```

In `sky_ground`, replace

```python
    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    g.emissive(g.mul(g.mul(colour, brightness), g.mul(shaded, factor)))
```

with

```python
    shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))
    # The tile's vertices carry their own shadow in UV0.x (TerrainTile::UV0Of);
    # the map's is blended into it by the morph that grows the relief in, so at
    # the handover the ground's shadow is the orbit's exactly.
    vertex = mask(g, g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0), "r")
    shadow = sun_shadow(g, direction, face_footprint(g, footprint), default_texture, vertex, morph)
    g.emissive(g.mul(g.mul(colour, brightness), g.mul(cast(g, shaded, shadow, g.scalar("shadows", 1.0)), factor)))
```

and update both docstrings' formulas: `shaded = gain * saturate(N.L) * smoothstep(-w, w, N.L) * lerp(1, shadow, Shadows)`, with `shadow = the map at D` for the body and `lerp(the map at D, UV0.x, Morph)` for the ground.

(e) Add the probe:

```python
def shadow_probe(default_texture):
    """Eyes.WorldReliefParity's cast-shadow case: the map's lookup over the
    probe patch (probe_direction) at ProbeFootprint, untonemapped.

        pixel = ProbeBias.rgb + (the map at D, 0, 0)"""
    asset = "M_SkyShadowProbe"
    material = fresh_material(asset)
    material.set_editor_property("allow_negative_emissive_color", True)
    g = Graph(material)
    footprint = g.scalar("probe_footprint", 0.0)
    bias = g.vector("probe_bias", (0.0, 0.0, 0.0, 0.0))
    direction = probe_direction(g)
    shadow = sun_shadow(g, direction, footprint, default_texture, g.constant(1.0), g.constant(0.0))
    pixel = g.binary(unreal.MaterialExpressionAppendVector,
                     g.binary(unreal.MaterialExpressionAppendVector, shadow, g.constant(0.0)), g.constant(0.0))
    g.emissive(g.add(mask(g, bias, "rgb"), pixel))
    finish(material, asset)
```

(f) In `finish`, after the vectors' comparison, add:

```python
    textures = sorted(str(n) for n in MEL.get_texture_parameter_names(material))
    want_textures = sorted(name(r) for r in expected if CONTRACT["parameters"][r]["type"] == "texture")
    if textures != want_textures:
        raise RuntimeError("%s does not match its contract: want textures %s, has %s" % (asset, want_textures, textures))
```

(g) In `main()`, before `sky_body()`, add `white = shadow_default_texture()`. Then replace the calls `sky_body()` with `sky_body(white)` and `sky_ground()` with `sky_ground(white)`, and after `sky_ground_probe()` add `shadow_probe(white)`. The probe passes `ProbeFootprint` to the node as it is: it is already a pixel's footprint, and the parity test hands `SunShadowMap::Sample` the same number.

- [ ] **Step 7: The sky's bakes.** The maps are **not** tied to `RebuildFor`. `RebuildFor` runs on every new jump serial, and `ShipNavState.cpp` bumps that serial on an in-system arrival too. Tied to it, every in-system jump would throw away every world's map and re-bake the system: about 15-30 s of two workers for the corpus's 3.75 solid worlds, with every world unshadowed meanwhile, though neither input to a map had moved. And `ds.Universe.ReloadPriors` changes a relief without changing the serial, so a map tied to the serial would go stale under a ground that had rebuilt. So each map is keyed by what it is made from, and the key is checked every frame.

In `Source/DeepSpace/Sky/ShipSky.h`:

(a) Add `#include <atomic>`, `#include "RenderCommandFence.h"`, `#include "Surface/SunShadowMap.h"`, `#include "Surface/WorldReliefParams.h"` and `#include "Tasks/Task.h"`. Add `class UTexture2D;` with the forward declarations.

(b) In the public section, after `GetNeighbourStars()`, add:

```cpp
    /** Launch every waiting shadow bake, wait for them all (and for any let
     *  go), upload them, and discard their CPU copies: for tests, which must
     *  see a world's map rather than a frame without. Maps landed this way
     *  are already faded in, so a judged frame never catches a fade. */
    void FlushShadowBakesForTest();

    /** The body's cast-shadow texture once its bake has landed; null before,
     *  for a body with no ground, and with ds.Sky.ShadowMaps 0. */
    UTexture2D* GetShadowTexture(FName Body) const;

    /** The map the texture was made from, kept only while
     *  bKeepShadowMapsForTest: a map is otherwise dropped once uploaded. */
    const FSunShadowMap* GetShadowMapForTest(FName Body) const;
    bool bKeepShadowMapsForTest = false;

    /** Bakes waiting or in flight (not those let go). */
    int32 GetShadowBakesPending() const;

    /** Every bake ever launched: a jump that re-baked nothing leaves it where it was. */
    int32 GetShadowBakesStarted() const { return ShadowBakesStarted; }

    /** The game-thread seconds the slowest map's landing took (its texture's
     *  creation and upload call), since play began, for Eyes.ShadowBakeCost. */
    double GetSlowestShadowLandSeconds() const { return SlowestShadowLandSeconds; }
```

(c) In the protected section, add `virtual void EndPlay(const EEndPlayReason::Type Reason) override;`.

(d) In the private section, after `FSkyFrame LastFrame;`, add:

```cpp
    /**
     * The cast shadow's maps (the developer's ruling on slice (b)'s build:
     * baked in C++, off the game thread). A cache of this actor's drawing,
     * keyed -- unlike the proxies -- not by the jump serial but by what each
     * map is made from (ShipSky::FShadowKey): the body's relief, the sky's
     * light for it and the width. SyncShadowMaps checks every solid world's
     * key each frame outside transit and re-bakes only a map whose key
     * changed, so a jump re-bakes nothing that did not change. One bake a
     * world, nearest first, at most ds.Sky.ShadowBakeTasks at once, at
     * BackgroundLow (the terrain's builds go first), each with its own cancel
     * flag, polled once a column. A map dropped mid-bake is cancelled and let
     * go (ShadowDraining), never waited on, and counted against the cap until
     * it stops.
     */
    struct FShadowFrame
    {
        FLinearColor X;
        FLinearColor Z;
    };
    struct FShadowEntry
    {
        ShipSky::FShadowKey Key;
        TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> Cancel;
        UE::Tasks::TTask<TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>> Task;   // valid while in flight
        FShadowFrame Frame;
        double LandedAt = 0.0;   // world seconds its texture landed: the fade runs from here
    };
    TMap<FName, FShadowEntry> ShadowEntries;
    TArray<FName> ShadowQueue;   // waiting, nearest first
    TArray<UE::Tasks::TTask<TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>>> ShadowDraining;
    /** Textures whose CPU copy goes once the render thread has them. */
    TArray<TPair<FName, TUniquePtr<FRenderCommandFence>>> ShadowUploads;
    int32 ShadowBakesStarted = 0;
    double SlowestShadowLandSeconds = 0.0;

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UTexture2D>> ShadowTextures;

    TMap<FName, TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>> KeptShadowMaps;

    /** Every solid world's key against the map held for it: a changed one is
     *  dropped and queued again, a world no longer here dropped. */
    void SyncShadowMaps(const FSkySystem& System);
    /** Cancel and let go of the body's bake, and drop its map. */
    void DropShadowMap(FName Body);
    /** Land what has finished, discard uploaded CPU copies, and launch what
     *  the cap allows; with bWait, wait for those in flight first. */
    void PumpShadowBakes(bool bWait);
```

(e) In `namespace ShipSky`, after `CopyBodyLook`, add the block below. `ShipSky`'s namespace comes after `class AShipSky` in the header, and the class's `FShadowEntry` names `FShadowKey`. So `FShadowKey` and `SameShadowKey` go in a second `namespace ShipSky { ... }` block *before* the class, and the rest go where shown.

```cpp
    /** ds.Sky.Shadows, clamped to 0..1: the cast shadow's strength in both
     *  materials, lerp(1, shadow, strength). 0 draws the unshadowed look. */
    DEEPSPACE_API float ShadowStrength();

    /** How long a landed map takes to fade its world's shadow in: the sky's
     *  rule that nothing changes in one frame. */
    inline constexpr double ShadowFadeSeconds = 1.0;

    /** The fade SecondsSinceLanded after a map landed, 0..1, linear. */
    DEEPSPACE_API float ShadowFade(double SecondsSinceLanded);

    /** ds.Sky.ShadowMapWidth as baked: a power of two, 256..8192. */
    DEEPSPACE_API int32 ShadowMapWidth();

    /** What a world's map is made from. A map is re-baked exactly when this
     *  changes -- never on a jump that moves none of it. */
    struct FShadowKey
    {
        FWorldReliefParams Relief;
        SunShadow::FSunLight Sun;
        double SteepestSlope = 0.0;
        int32 Width = 0;
    };
    DEEPSPACE_API bool SameShadowKey(const FShadowKey& A, const FShadowKey& B);

    /** The bodies whose shadow maps are baked -- the solid worlds -- nearest
     *  to Ship's surface first. */
    DEEPSPACE_API TArray<int32> ShadowBakeOrder(const FSkySystem& System, const FUniversePosition& Ship);

    /** The map's frame as the materials take it: X with PsiLo in w, and Z,
     *  the light, with Step in w. */
    DEEPSPACE_API FLinearColor ShadowFrameX(const FSunShadowMap& Map);
    DEEPSPACE_API FLinearColor ShadowFrameZ(const FSunShadowMap& Map);

    /** A transient G16 texture of the map, linear, grayscale, each level its
     *  own mip, never streamed, in TEXTUREGROUP_Pixels2D (no device profile
     *  biases it: a dropped mip 0 would put every lookup, whose U is in
     *  level-0 texels, in the wrong place). Null for a map with no levels. */
    DEEPSPACE_API UTexture2D* MakeShadowTexture(const FSunShadowMap& Map, FName Name);

    /** The bytes a texture's mips still hold on the CPU, and letting them go
     *  once the render thread has the texture. A transient texture otherwise
     *  keeps them after UpdateResource, doubling each map's cost. After the
     *  discard the resource cannot be rebuilt from the CPU; nothing here
     *  rebuilds it (NeverStream, an unbiased group). */
    DEEPSPACE_API int64 ShadowTextureCpuBytes(const UTexture2D& Texture);
    DEEPSPACE_API void DiscardShadowTextureCpu(UTexture2D& Texture);
```

Also amend `CopyBodyLook`'s comment: "... and the cast shadow's map, frame and strength".

In `Source/DeepSpace/Sky/ShipSky.cpp`:

(f) Add the includes `#include "Engine/Texture2D.h"`, `#include "HAL/PlatformTime.h"`, `#include "Surface/GroundField.h"`, `#include "Surface/SunShadow.h"`, `#include "TextureResource.h"` and `#include "UObject/Package.h"`. With the other CVars, add:

```cpp
    TAutoConsoleVariable<float> CVarShadows(
        TEXT("ds.Sky.Shadows"), 1.0f,
        TEXT("The cast shadow's strength in M_SkyBody and M_SkyGround, 0..1: 0 draws the unshadowed look. Baked, so it costs nothing either way."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowMaps(
        TEXT("ds.Sky.ShadowMaps"), 1,
        TEXT("1 bakes each solid world's cast-shadow map, off the game thread, re-baking one only when its relief, its light or the width changes; 0 bakes none and drops those held."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowMapWidth(
        TEXT("ds.Sky.ShadowMapWidth"), SunShadowMap::DefaultWidth,
        TEXT("Columns of each world's cast-shadow map, a power of two from 256 to 8192 (4096: 8.8 km texels on Baemsekai IV). Changing it re-bakes every map."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarShadowBakeTasks(
        TEXT("ds.Sky.ShadowBakeTasks"), 2,
        TEXT("Cast-shadow maps baking at once, a world a task, on worker threads at low priority: with the terrain's 2, the machine's cap of 4, never the core count."),
        ECVF_Default);
```

(g) `AShipSky::RebuildFor` is **unchanged**: the maps are not the proxies' and outlive a rebuild. The new proxies' instances are handed the held maps by `DrawBodies` in the same frame.

(h) In `AShipSky::SyncTo`, as its first statements, add:

```cpp
    // The cast shadow's maps: each world's key against the map held for it,
    // outside transit (between stars the system reads empty, and a map baked
    // for the system being left is still that system's), then land and
    // launch.
    if (!bInTransit)
    {
        SyncShadowMaps(System);
    }
    PumpShadowBakes(false);
```

(i) Add the member functions:

```cpp
void AShipSky::EndPlay(const EEndPlayReason::Type Reason)
{
    // Cancelled and let go: the bakes hold no this, and stop within a column.
    TArray<FName> Held;
    ShadowEntries.GetKeys(Held);
    for (const FName Body : Held)
    {
        DropShadowMap(Body);
    }
    ShadowDraining.Reset();
    ShadowUploads.Reset();
    Super::EndPlay(Reason);
}

void AShipSky::SyncShadowMaps(const FSkySystem& System)
{
    TSet<FName> Wanted;
    if (CVarShadowMaps.GetValueOnGameThread() != 0)
    {
        const UShipSubsystem* Ship = UShipSubsystem::Get(this);
        const FUniversePosition From = Ship ? Ship->GetFlightState().GetUniversePosition() : FUniversePosition();
        const int32 Width = ShipSky::ShadowMapWidth();
        for (const int32 Index : ShipSky::ShadowBakeOrder(System, From))
        {
            const FSkyBody& Body = System.Bodies[Index];
            Wanted.Add(Body.Id);
            ShipSky::FShadowKey Key;
            Key.Relief = Body.Relief;
            Key.Sun = SkyProjection::SunLightOf(System, Index);
            Key.SteepestSlope = SunShadow::SteepestSlope(Body.Relief);
            Key.Width = Width;
            const FShadowEntry* Held = ShadowEntries.Find(Body.Id);
            if (Held && ShipSky::SameShadowKey(Held->Key, Key))
            {
                continue;   // made from the same things: nothing to bake
            }
            DropShadowMap(Body.Id);
            ShadowEntries.Add(Body.Id).Key = Key;
            ShadowQueue.Add(Body.Id);
        }
    }
    TArray<FName> Gone;
    for (const TPair<FName, FShadowEntry>& Pair : ShadowEntries)
    {
        if (!Wanted.Contains(Pair.Key))
        {
            Gone.Add(Pair.Key);
        }
    }
    for (const FName Body : Gone)
    {
        DropShadowMap(Body);
    }
}

void AShipSky::DropShadowMap(FName Body)
{
    if (FShadowEntry* Entry = ShadowEntries.Find(Body))
    {
        if (Entry->Cancel.IsValid())
        {
            Entry->Cancel->store(true, std::memory_order_relaxed);
        }
        if (Entry->Task.IsValid())
        {
            ShadowDraining.Add(MoveTemp(Entry->Task));
        }
        ShadowEntries.Remove(Body);
    }
    ShadowQueue.Remove(Body);
    ShadowTextures.Remove(Body);
    KeptShadowMaps.Remove(Body);
    ShadowUploads.RemoveAll([Body](const TPair<FName, TUniquePtr<FRenderCommandFence>>& Upload) { return Upload.Key == Body; });
}

int32 AShipSky::GetShadowBakesPending() const
{
    int32 Pending = ShadowQueue.Num();
    for (const TPair<FName, FShadowEntry>& Pair : ShadowEntries)
    {
        Pending += Pair.Value.Task.IsValid() ? 1 : 0;
    }
    return Pending;
}

void AShipSky::PumpShadowBakes(bool bWait)
{
    using FBakeTask = UE::Tasks::TTask<TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>>;
    if (bWait)
    {
        for (FBakeTask& Task : ShadowDraining)
        {
            Task.Wait();
        }
    }
    ShadowDraining.RemoveAll([](const FBakeTask& Task) { return Task.IsCompleted(); });

    // Launch: what was let go still holds a worker until it stops.
    const int32 Cap = FMath::Max(1, CVarShadowBakeTasks.GetValueOnGameThread());
    int32 Busy = ShadowDraining.Num() + (GetShadowBakesPending() - ShadowQueue.Num());
    while (Busy < Cap && ShadowQueue.Num() > 0)
    {
        const FName Body = ShadowQueue[0];
        ShadowQueue.RemoveAt(0);
        FShadowEntry& Entry = ShadowEntries.FindChecked(Body);
        Entry.Cancel = MakeShared<std::atomic<bool>, ESPMode::ThreadSafe>(false);
        const ShipSky::FShadowKey Key = Entry.Key;
        const TSharedPtr<std::atomic<bool>, ESPMode::ThreadSafe> Cancel = Entry.Cancel;
        Entry.Task = UE::Tasks::Launch(UE_SOURCE_LOCATION,
            [Key, Cancel]() -> TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>
            {
                const FReliefGround Ground(Key.Relief);
                return MakeShared<FSunShadowMap, ESPMode::ThreadSafe>(SunShadowMap::Bake(Ground, Key.Sun, Key.SteepestSlope, Key.Width, Cancel.Get()));
            }, UE::Tasks::ETaskPriority::BackgroundLow);
        ++Busy;
        ++ShadowBakesStarted;
    }

    // Land.
    const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
    for (TPair<FName, FShadowEntry>& Pair : ShadowEntries)
    {
        FShadowEntry& Entry = Pair.Value;
        if (!Entry.Task.IsValid())
        {
            continue;
        }
        if (bWait)
        {
            Entry.Task.Wait();
        }
        if (!Entry.Task.IsCompleted())
        {
            continue;
        }
        const TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe> Map = Entry.Task.GetResult();
        Entry.Task = FBakeTask();
        if (!Map.IsValid() || Map->LevelCount() == 0)
        {
            continue;
        }
        const double Began = FPlatformTime::Seconds();
        UTexture2D* Texture = ShipSky::MakeShadowTexture(*Map, Pair.Key);
        SlowestShadowLandSeconds = FMath::Max(SlowestShadowLandSeconds, FPlatformTime::Seconds() - Began);
        if (!Texture)
        {
            continue;
        }
        ShadowTextures.Add(Pair.Key, Texture);
        Entry.Frame = { ShipSky::ShadowFrameX(*Map), ShipSky::ShadowFrameZ(*Map) };
        // In play the fade starts now; a test's flush lands it whole.
        Entry.LandedAt = bWait ? Now - ShipSky::ShadowFadeSeconds : Now;
        TUniquePtr<FRenderCommandFence> Fence = MakeUnique<FRenderCommandFence>();
        Fence->BeginFence();
        ShadowUploads.Emplace(Pair.Key, MoveTemp(Fence));
        if (bKeepShadowMapsForTest)
        {
            KeptShadowMaps.Add(Pair.Key, Map);
        }
    }

    // Once the render thread has a texture, its CPU copy is dead weight.
    for (int32 Index = ShadowUploads.Num() - 1; Index >= 0; --Index)
    {
        if (bWait)
        {
            ShadowUploads[Index].Value->Wait();
        }
        if (!ShadowUploads[Index].Value->IsFenceComplete())
        {
            continue;
        }
        if (const TObjectPtr<UTexture2D>* Texture = ShadowTextures.Find(ShadowUploads[Index].Key))
        {
            ShipSky::DiscardShadowTextureCpu(*Texture->Get());
        }
        ShadowUploads.RemoveAt(Index);
    }
}

void AShipSky::FlushShadowBakesForTest()
{
    do
    {
        PumpShadowBakes(true);
    }
    while (GetShadowBakesPending() > 0 || ShadowUploads.Num() > 0);
}

UTexture2D* AShipSky::GetShadowTexture(FName Body) const
{
    const TObjectPtr<UTexture2D>* Found = ShadowTextures.Find(Body);
    return Found ? Found->Get() : nullptr;
}

const FSunShadowMap* AShipSky::GetShadowMapForTest(FName Body) const
{
    const TSharedPtr<FSunShadowMap, ESPMode::ThreadSafe>* Found = KeptShadowMaps.Find(Body);
    return Found && Found->IsValid() ? Found->Get() : nullptr;
}
```

(j) In `DrawBodies`, inside `if (System.Bodies[Index].Kind != ESkyBodyKind::Star)`, after the two `BodyAxis` writes, add:

```cpp
            // The cast shadow: the strength every frame, faded in from the
            // frame the map landed; the map and its frame once it has.
            const FName Id = System.Bodies[Index].Id;
            const FShadowEntry* Shadow = ShadowEntries.Find(Id);
            const TObjectPtr<UTexture2D>* Map = ShadowTextures.Find(Id);
            const float Fade = Shadow && Map ? ShipSky::ShadowFade(GetWorld()->GetTimeSeconds() - Shadow->LandedAt) : 0.0f;
            Instance->SetScalarParameterValue(SkyMaterial::Shadows, ShipSky::ShadowStrength() * Fade);
            if (Shadow && Map)
            {
                Instance->SetTextureParameterValue(SkyMaterial::ShadowMap, Map->Get());
                Instance->SetVectorParameterValue(SkyMaterial::ShadowFrameX, Shadow->Frame.X);
                Instance->SetVectorParameterValue(SkyMaterial::ShadowFrameZ, Shadow->Frame.Z);
            }
```

(`Shadow`, not `Frame`: `DrawBodies` already has a parameter `Frame`, the `FSkyFrame`.) A world whose map was dropped keeps its old texture on the instance until a new one lands, but at strength 0, so it draws unshadowed. The instance's reference holds that texture's GPU memory until then, or until a new system's rebuild makes new instances.

(k) Replace `ShipSky::CopyBodyLook`'s body with:

```cpp
    for (const FName Name : { SkyMaterial::Colour, SkyMaterial::LightDirection, SkyMaterial::SurfaceSeed, SkyMaterial::ShadowFrameX, SkyMaterial::ShadowFrameZ })
    {
        To.SetVectorParameterValue(Name, From.K2_GetVectorParameterValue(Name));
    }
    for (const FName Name : { SkyMaterial::Brightness, SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::ReliefScale, SkyMaterial::Cratering, SkyMaterial::Shadows })
    {
        To.SetScalarParameterValue(Name, From.K2_GetScalarParameterValue(Name));
    }
    To.SetTextureParameterValue(SkyMaterial::ShadowMap, From.K2_GetTextureParameterValue(SkyMaterial::ShadowMap));
```

The ground copies the faded strength, so it fades in with the orbit.

(l) Add the pure helpers at the end of the file:

```cpp
float ShipSky::ShadowStrength()
{
    return FMath::Clamp(CVarShadows.GetValueOnGameThread(), 0.0f, 1.0f);
}

float ShipSky::ShadowFade(double SecondsSinceLanded)
{
    return static_cast<float>(FMath::Clamp(SecondsSinceLanded / ShadowFadeSeconds, 0.0, 1.0));
}

int32 ShipSky::ShadowMapWidth()
{
    return static_cast<int32>(FMath::RoundUpToPowerOfTwo(static_cast<uint32>(FMath::Clamp(CVarShadowMapWidth.GetValueOnGameThread(), 256, 8192))));
}

bool ShipSky::SameShadowKey(const FShadowKey& A, const FShadowKey& B)
{
    return SameRelief(A.Relief, B.Relief) && A.Sun.Direction == B.Sun.Direction && A.Sun.AngularRadius == B.Sun.AngularRadius
        && A.SteepestSlope == B.SteepestSlope && A.Width == B.Width;
}

TArray<int32> ShipSky::ShadowBakeOrder(const FSkySystem& System, const FUniversePosition& Ship)
{
    TArray<int32> Order;
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        if (System.Bodies[Index].Ground == EGround::Solid && System.Bodies[Index].Kind != ESkyBodyKind::Star)
        {
            Order.Add(Index);
        }
    }
    Order.StableSort([&](int32 A, int32 B)
    {
        return Ship.DistanceTo(System.Bodies[A].Position) - System.Bodies[A].Radius < Ship.DistanceTo(System.Bodies[B].Position) - System.Bodies[B].Radius;
    });
    return Order;
}

FLinearColor ShipSky::ShadowFrameX(const FSunShadowMap& Map)
{
    return FLinearColor(static_cast<float>(Map.FrameX.X), static_cast<float>(Map.FrameX.Y), static_cast<float>(Map.FrameX.Z), static_cast<float>(Map.PsiLo));
}

FLinearColor ShipSky::ShadowFrameZ(const FSunShadowMap& Map)
{
    return FLinearColor(static_cast<float>(Map.FrameZ.X), static_cast<float>(Map.FrameZ.Y), static_cast<float>(Map.FrameZ.Z), static_cast<float>(Map.Step));
}

UTexture2D* ShipSky::MakeShadowTexture(const FSunShadowMap& Map, FName Name)
{
    if (Map.LevelCount() == 0)
    {
        return nullptr;
    }
    const TArray<uint16>& Finest = Map.Levels[0];
    const FName Unique = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(),
        FName(*(TEXT("ShadowMap_") + Name.ToString().Replace(TEXT(" "), TEXT("_")))));
    UTexture2D* Texture = UTexture2D::CreateTransient(Map.Width, Map.Rows, PF_G16, Unique,
        TConstArrayView64<uint8>(reinterpret_cast<const uint8*>(Finest.GetData()), static_cast<int64>(Finest.Num()) * sizeof(uint16)));
    if (!Texture)
    {
        return nullptr;
    }
    FTexturePlatformData* Data = Texture->GetPlatformData();
    for (int32 Level = 1; Level < Map.LevelCount(); ++Level)
    {
        const TArray<uint16>& Texels = Map.Levels[Level];
        FTexture2DMipMap* Mip = new FTexture2DMipMap(Map.WidthAt(Level), Map.RowsAt(Level), 1);
        Data->Mips.Add(Mip);
        Mip->BulkData.Lock(LOCK_READ_WRITE);
        FMemory::Memcpy(Mip->BulkData.Realloc(static_cast<int64>(Texels.Num()) * sizeof(uint16)), Texels.GetData(), Texels.Num() * sizeof(uint16));
        Mip->BulkData.Unlock();
    }
    // Grayscale and linear, as the parameters' default is (SAMPLERTYPE_GRAYSCALE);
    // in a group no device profile biases, so mip 0 is always resident.
    Texture->CompressionSettings = TC_Grayscale;
    Texture->SRGB = false;
    Texture->Filter = TF_Nearest;
    Texture->LODGroup = TEXTUREGROUP_Pixels2D;
    Texture->NeverStream = true;
    Texture->UpdateResource();
    return Texture;
}

int64 ShipSky::ShadowTextureCpuBytes(const UTexture2D& Texture)
{
    int64 Bytes = 0;
    if (const FTexturePlatformData* Data = Texture.GetPlatformData())
    {
        for (const FTexture2DMipMap& Mip : Data->Mips)
        {
            Bytes += Mip.BulkData.GetBulkDataSize();
        }
    }
    return Bytes;
}

void ShipSky::DiscardShadowTextureCpu(UTexture2D& Texture)
{
    if (FTexturePlatformData* Data = Texture.GetPlatformData())
    {
        for (FTexture2DMipMap& Mip : Data->Mips)
        {
            Mip.BulkData.RemoveBulkData();
        }
    }
}
```

`SameRelief` is `WorldReliefParams.h`'s (Task 4). If `RemoveBulkData` is refused on a transient mip (an assert, or the size unchanged), say so in the task report and keep the copy. Task 7 then measures the doubled memory, and a budget over goes to Task 7b. Never keep the copy silently.

- [ ] **Step 8: Build and author.**

The header changes here (`ShipSky.h`) and Task 4's (`WorldReliefParams.h`, `WorldGround.h`, `TerrainTile.h`) need `./rebuild.sh --force` before an editor session.

Run:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; \
cat Saved/setup_sky_materials.txt | tail -12
```

Expected:
- The report ends `ok` and lists `T_SkyShadowWhite`, then `M_SkyBody`, `M_SkyGround` and `M_SkyShadowProbe`, each with its scalars and vectors.
- A contract mismatch raises and names it.
- If the import names a sampler-type mismatch for `T_SkyShadowWhite` (a grayscale sampler over a texture that is not TC_Grayscale), the import did not take the settings. Check the asset's compression, and never switch the sampler type to hide it.

- [ ] **Step 9: Run the tests.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./test.sh DeepSpace.Sky.ShadowParameters && ./test.sh DeepSpace.Sky.MaterialContract && ./test.sh DeepSpace.Sky && ./test.sh DeepSpace.Surface`

Expected: all PASS.
- `.MaterialContract` translates every graph, the three shadow nodes included. **A Custom node's HLSL error does not show here**: the translator passes and no shader compiles under `-nullrhi`. It shows in Task 8's `Eyes.WorldReliefParity`, which is the first thing to run after this commit.
- If `.ShadowParameters` fails on `GetNumMips` under `-nullrhi`, read the count from `Texture->GetPlatformData()->Mips.Num()` instead, and say so in the test's comment.
- If it fails on "no copy left on the CPU" under `-nullrhi`, check that the fence completed (`FlushShadowBakesForTest` waits on it) before doubting `RemoveBulkData`. Never drop the check.
- If the fade leg's landing frame reads a strength over 0, the landing and `DrawBodies` ran in different frames: `PumpShadowBakes` must run at the top of `SyncTo`, before `DrawBodies`.

- [ ] **Step 10: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Tools/sky_material_contract.json Tools/setup_sky_materials.py Source/DeepSpace/Sky/SkyMaterialContract.h \
  Source/DeepSpace/Tests/SkyMaterialContractTest.cpp Source/DeepSpace/Sky/ShipSky.h Source/DeepSpace/Sky/ShipSky.cpp \
  Source/DeepSpace/Tests/ShipSkyShadowTest.cpp Content/Materials/Sky && \
git commit -qm "feat(sky): the orbit and the ground read each world's baked cast shadow

AShipSky bakes every solid world's SunShadowMap off the game thread, keyed
by what the map is made from (relief, the sky's light, the width) and
checked each frame: a jump re-bakes nothing, a reload of the priors re-bakes
the worlds whose relief moved, and a dropped bake is cancelled and let go,
never waited on (nearest first, ds.Sky.ShadowBakeTasks at once, at low
priority). It uploads a G16 texture with a mip a level in an unbiased LOD
group, discards the CPU copy once uploaded, and hands the texture, its frame
and ds.Sky.Shadows -- faded in over a second -- to M_SkyBody; CopyBodyLook
carries them to M_SkyGround, which blends the map into its vertices' shadow
by Morph. One Custom node over the shared file's lookup in both, handed a
pixel's footprint, and in the parity probe M_SkyShadowProbe.
DeepSpace.Sky.ShadowParameters; the contract's textures.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 11: Prove it.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'return FMath::Clamp(CVarShadows.GetValueOnGameThread(), 0.0f, 1.0f);' 'return CVarShadows.GetValueOnGameThread();' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '    To.SetTextureParameterValue(SkyMaterial::ShadowMap, From.K2_GetTextureParameterValue(SkyMaterial::ShadowMap));' '' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'Ship.DistanceTo(System.Bodies[A].Position) - System.Bodies[A].Radius < Ship.DistanceTo' 'Ship.DistanceTo(System.Bodies[A].Position) - System.Bodies[A].Radius > Ship.DistanceTo' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '    if (CVarShadowMaps.GetValueOnGameThread() != 0)' '    if (true)' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '            if (Held && ShipSky::SameShadowKey(Held->Key, Key))' '            if (false)' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '    return SameRelief(A.Relief, B.Relief) && A.Sun.Direction' '    return A.Sun.Direction' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'Instance->SetScalarParameterValue(SkyMaterial::ShadowMapFade, Fade);' 'Instance->SetScalarParameterValue(SkyMaterial::ShadowMapFade, 1.0f);' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '    Texture->LODGroup = TEXTUREGROUP_Pixels2D;' '' 'DeepSpace.Sky.ShadowParameters$'; \
./build.sh
```

Expected: eight `KILLED`.

**Amended after review (2026-09-28):** the fade is the map's alone. As first built, `Shadows` was the strength times the fade and the ground copied it, so `M_SkyGround` multiplied its vertices' own shadow out until a map had landed, and always under `ds.Sky.ShadowMaps 0`. `Shadows` is now the strength; a scalar `ShadowMapFade` (on all three sides) fades the map in, `lerp(lerp(1, Vertex, Morph), node, ShadowMapFade)` in both materials (`setup_sky_materials.py`'s `map_fade`), and `CopyBodyLook` carries it. The seventh mutant above is the fade's, rewritten from the strength-times-fade line that no longer exists; it was run and KILLED. `DeepSpace.Surface.GroundShadowLight` holds the look at full strength with no map.

- The unclamped strength draws 7.
- The ground that is not handed the map reads the white default.
- The bakes ordered farthest first break the order.
- The switch that is never read bakes with `ds.Sky.ShadowMaps` 0.
- The key never matched re-bakes every map every frame, so a jump within the system is not free.
- The key blind to relief keeps a stale map through a reload of the priors.
- The strength with no fade draws the landing frame whole.
- The texture in the default group fails the LOD group's check.

That a dropped bake is never waited on is a property of the code, not of a value, so no headless mutant can see it. Task 7 times the jump and the drop.

The material side is proven on the GPU by Tasks 8 and 9, after Task 7 has measured the cost.

---
## Task 7: THE COST, measured and budgeted -- `Eyes.ShadowBakeCost`

**Owner:** T. **Depends on:** Task 6 (the sky's bakes) and Task 4 (the tiles'), and Task 0's third answer (the budgets). This measures, and changes no shader and no game code. The one exception is that it moves `DeepSpace.Surface.GroundKeepsUp`'s flight into a shared header, so the same flight is flown with the shadow on.

**Files:**
- Create: `Source/DeepSpace/Tests/GroundKeepsUpScenario.h`. This is `GroundKeepsUp`'s flight, moved out unchanged: paced to the wall clock, from the drive floor at the full sink, then the skim cap's top at 500 m and at 50 m. It returns its counts.
- Modify: `Source/DeepSpace/Tests/GroundKeepsUpTest.cpp`, which flies it through the header with the shadow off, as before, and asserts what it asserted.
- Create: `Source/DeepSpace/Tests/Eyes/ShadowBakeCostEyesTest.cpp` (`Eyes.ShadowBakeCost`)

**Interfaces:**
- Consumes:
  - `TerrainTile::{Build, FTileShadow}` and `AWorldGround::{GetTileShadow, GetDrawnKeys, GetResidentCount, GetTileShadowBytes, GetBuildingCount, FlushBuildsForTest, IsDrawingBody}` (Task 4);
  - `SunShadowMap::Bake` and `FSunShadowMap::Bytes` (Task 5);
  - `AShipSky::{FlushShadowBakesForTest, GetShadowTexture, GetShadowMapForTest, bKeepShadowMapsForTest, GetShadowBakesStarted, GetSlowestShadowLandSeconds}`, `ShipSky::ShadowTextureCpuBytes`, and `ds.Sky.ShadowMapWidth` and `ds.Sky.ShadowBakeTasks` (Task 6);
  - `SkyProjection::SunLightOf` (Task 3), `SunShadow::SteepestSlope`, `ShipSky::GotoPlacement`, and `SkyTestWorld::{FSkyWorld, EShadows, FScopedCVar}`.
- Produces:
  - `GroundKeepsUpScenario::{FResult, Fly}`;
  - `Saved/Eyes/ShadowBakeCost/report.txt`, with lines `cut ...`, `release ...`, `tile ...`, `map ...`, `system ...`, `jump ...` and `flight ...`, and one `budget <name> <measured> <limit> within|OVER` line for each budget.

**The budgets, decided now** (Task 0's third question; these are its recommended defaults, and they are replaced here by its answers):

| Budget | Limit | Why |
|---|---|---|
| `cold_cut_s`: the cut at 1.5 m over IV at a 10-degree dusk, resident from nothing, with the shadow, on `ds.Terrain.BuildTasks` 2 | 30 s | planning's 27 s estimate at 12 samples; residency never gates motion, and the coarse tiles come first |
| `release_ms`: the game-thread time a ground's release with a full set of shadowed builds in flight adds to its frame, over an ordinary frame's median | 2 ms | it runs on every fold opened near a world; a wait on a build is 16-70 ms |
| `tile_ratio`: a tile's median build with the shadow over without, 64 of that cut's keys, 10-degree light | 10 x | planning's 7.7-9.4 x |
| `world_bake_s`: one world's map on one thread at `ds.Sky.ShadowMapWidth`, the slowest of the start system's I-V and of the corpus's two extremes | 15 s | a world is shadowed well before a ship at the arrival standoff can reach it |
| `world_map_mb`: one world's map, level 0 and every mip, the largest of the same | 16 MB | 4096 columns: planning's 12 MB; the deepest `PsiLo` has the most rows |
| `system_gpu_mb`: the start system's maps resident on the GPU, measured through the engine | 64 MB | 5 worlds x about 12 MB; the corpus's worst system (16 worlds) is reported, not held |
| `system_cpu_mb`: what the start system's textures still hold on the CPU once uploaded | 0 MB | the copy is discarded (Task 6) |
| `map_land_ms`: the slowest map's landing on the game thread (texture creation and upload call) | 4 ms | it happens in play, up to a world's bake time after the system loads |
| `jump_rebakes`: bakes started by an in-system jump, arrival and rebuild included | 0 | a jump moves neither input to any map |
| `flight_missing`: frames under 1 km with no drawn ground under the ship, `GroundKeepsUp`'s flight with the shadow on | 0 | the invariant `GroundKeepsUp` holds with it off |
| `flight_worst_cm`: the worst drawn gap under 1 km in that flight | GearClearance / 10 | the same |
| `flight_not_drawing`: frames under the drive floor where the ground does not draw the body, in that flight | 0 | the same |
| `coarse_s`: from arriving 49 km over fresh ground to the ground drawing the body, paced to the wall clock | 10 s | the 50 km handover waits on it |
| `tile_shadow_mb`: the resident cut's vertex shadows on the CPU, both copies, from the arrays | 12 MB | 951 tiles x 1,089 floats x 2 copies = 8.3 MB |

The frame is `Eyes.LandingFrame`'s, in Task 9: it must not rise.

- [ ] **Step 1: The flight, shared.** Create `Source/DeepSpace/Tests/GroundKeepsUpScenario.h`:

```cpp
#pragma once

#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * DeepSpace.Surface.GroundKeepsUp's flight, shared so Eyes.ShadowBakeCost
 * flies the same one with the cast shadow on: over Baemsekai IV, from the
 * drive floor at the full sink, then the skim cap's top at 500 m and at 50
 * m, each frame paced to the wall clock so the workers get the time they get
 * in play. It counts; the callers judge.
 */
namespace GroundKeepsUpScenario
{
    struct FResult
    {
        int32 UnderFloor = 0;    // frames under the drive floor
        int32 ProxyShown = 0;    // of those, the proxy drawn
        int32 NotDrawing = 0;    // of those, the ground not drawing the body
        int32 Low = 0;           // frames under 1 km
        int32 Missing = 0;       // of those, no drawn ground under the ship
        double Worst = 0.0;      // the worst drawn gap under 1 km, cm
        double Fastest = 0.0;    // the descent's fastest sink, cm/s
        double Tolerance = 0.0;  // GearClearance / 10, cm
        TArray<double> Top;      // each skim leg's best share of the cap (500 m, 50 m)
        TArray<bool> HeldOff;    // and whether a ridge held it off
        bool bValid = false;
    };

    /** Fly it in Test, already begun: ds.Terrain.BuildTasks and
     *  UploadsPerFrame as the caller set them. */
    FResult Fly(SkyTestWorld::FSkyWorld& Test);
}

#endif
```

Move `GroundKeepsUpTest.cpp`'s flight into it as an inline `Fly`: everything from `UShipSubsystem* Ship = Test.Ship;` through the end of the skim loop. The frame lambda, the counters and the three phases go in unchanged. Each counter becomes the result's field, and each skim leg's `Top` and `bHeldOff` is appended to `Top` and `HeldOff`. Move the includes it needs with it (`GameFramework/Pawn.h`, `HAL/PlatformProcess.h`, `HAL/PlatformTime.h`, `Ship/ShipFlightSurface.h`, `Ship/ShipSubsystem.h`, `Sky/LocalSystem.h`, `Sky/ShipSky.h`, `Surface/GroundField.h`, `Surface/WorldGround.h`). `GroundKeepsUpTest.cpp` becomes:

```cpp
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
    const GroundKeepsUpScenario::FResult R = GroundKeepsUpScenario::Fly(Test);
    if (!TestTrue(TEXT("the flight was flown"), R.bValid && R.Top.Num() == 2 && R.HeldOff.Num() == 2))
    {
        return false;
    }
    TestTrue(FString::Printf(TEXT("the descent reached the full sink, 200 m/s (%.1f m/s)"), R.Fastest / 100.0), R.Fastest >= 0.99 * 2.0e4);
    for (int32 Leg = 0; Leg < 2; ++Leg)
    {
        const double Height = Leg == 0 ? 500.0 : 50.0;
        AddInfo(FString::Printf(TEXT("at %.0f m: cruise reached %.2f of the skim cap%s"), Height, R.Top[Leg], R.HeldOff[Leg] ? TEXT(", held off a ridge") : TEXT("")));
        TestTrue(FString::Printf(TEXT("at %.0f m the ship flew at the skim cap's top, or a ridge held it off (%.2f)"), Height, R.Top[Leg]),
                 R.Top[Leg] >= 0.9 || R.HeldOff[Leg]);
    }
    AddInfo(FString::Printf(TEXT("%d frames under the drive floor, %d under 1 km; worst drawn gap %.2f cm"), R.UnderFloor, R.Low, R.Worst));
    TestTrue(TEXT("the flight spent frames under the floor and under 1 km"), R.UnderFloor > 1000 && R.Low > 1000);
    TestEqual(TEXT("under the drive floor the proxy is never drawn"), R.ProxyShown, 0);
    TestEqual(TEXT("and the ground draws the body every frame"), R.NotDrawing, 0);
    TestEqual(TEXT("under 1 km there is always drawn ground under the ship"), R.Missing, 0);
    TestTrue(FString::Printf(TEXT("and it is within GearClearance / 10 of the analytic ground, moving (worst %.2f cm)"), R.Worst),
             R.Worst <= R.Tolerance);
    return true;
}
```

The test's messages are its old ones, word for word. Run `./test.sh DeepSpace.Surface.GroundKeepsUp`: it passes as before, in about the same two minutes. Its old mutation proofs still apply through the header.

- [ ] **Step 2: The measurement.** Create `Source/DeepSpace/Tests/Eyes/ShadowBakeCostEyesTest.cpp`:

```cpp
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Ship/ShipSubsystem.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyProjection.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/SunShadowMap.h"
#include "Surface/TerrainTile.h"
#include "Surface/WorldGround.h"
#include "Tests/GroundKeepsUpScenario.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * A MEASUREMENT, not a guard: what the baked cast shadow costs (the
 * cast-shadow plan, Task 7). Named outside DeepSpace. so ./test.sh never runs
 * it -- it is timed, and a timing belongs behind the lock on a quiet machine
 * -- and run as the rendered checks are:
 *
 *   Tools/eyes.sh Eyes.ShadowBakeCost
 *
 * It renders nothing. It measures:
 *
 * - The cold cut: the ground at 1.5 m over Baemsekai IV under a 10-degree
 *   dusk, resident from nothing, without and with the shadow
 *   (ds.Terrain.Shadows 0 then 1), on the ground's own tasks; and a release
 *   of the ground with shadowed builds in flight, on the game thread.
 * - A tile: 64 of that cut's keys, each built here, on this thread, without
 *   the shadow and with it under a 3-, 10- and 60-degree sun over the cut's
 *   centre. Medians and 90th percentiles of BuildSeconds, and their ratio.
 * - A world's map: Baemsekai I-V, and the corpus's two extremes -- the most
 *   relief for its size (Baiti I: 0.44 Earth radii, 9.74 km) and the
 *   largest solid world (Tishras I: 2.13, 2.57 km) -- at Cratering 1, the
 *   costliest, under IV's light, each baked here on one thread at
 *   ds.Sky.ShadowMapWidth.
 * - The system: the sky's own bakes of every solid world on
 *   ds.Sky.ShadowBakeTasks, the GPU memory they hold as the engine counts it
 *   (TMC_ResidentMips), what they still hold on the CPU, the process's
 *   physical memory before and after, and the slowest landing on the game
 *   thread; the corpus's worst system (16 worlds) by the same per-world
 *   figure, reported.
 * - A jump within the system: the bakes it starts.
 * - The flight: GroundKeepsUp's, with the shadow on, paced to the wall clock;
 *   and the wait, from 49 km over fresh ground, for the ground to draw the
 *   body.
 * - The resident cut's vertex shadows, both copies, from the arrays.
 *
 * Each budget prints one line, `budget <name> <measured> <limit> within|OVER`;
 * the plan's rule reads them. Nothing is asserted but that each measure was
 * taken, because a loaded machine would make a timing assertion a coin toss.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowBakeCostEyesTest, "Eyes.ShadowBakeCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShadowBakeCostLocal
{
    // The budgets: Task 0's answers (the plan's recommended defaults until then).
    constexpr double ColdCutBudgetSeconds = 30.0;
    constexpr double ReleaseBudgetMs = 2.0;
    constexpr double TileRatioBudget = 10.0;
    constexpr double WorldBakeBudgetSeconds = 15.0;
    constexpr double WorldMapBudgetMB = 16.0;
    constexpr double SystemGpuBudgetMB = 64.0;
    constexpr double SystemCpuBudgetMB = 0.0;
    constexpr double MapLandBudgetMs = 4.0;
    constexpr double JumpRebakesBudget = 0.0;
    constexpr double CoarseBudgetSeconds = 10.0;
    constexpr double TileShadowBudgetMB = 12.0;
    constexpr int32 WorstSystemWorlds = 16;   // Saved/procgen_corpus.tsv's most solid worlds in one system
    constexpr double EarthRadiusCm = 6.371e8;

    double Percentile(TArray<double> Values, double Share)
    {
        if (Values.Num() == 0)
        {
            return 0.0;
        }
        Values.Sort();
        return Values[FMath::Clamp(FMath::FloorToInt32(Share * (Values.Num() - 1)), 0, Values.Num() - 1)];
    }

    FString Budget(const TCHAR* Name, double Measured, double Limit)
    {
        return FString::Printf(TEXT("budget %s %.3f %.3f %s"), Name, Measured, Limit, Measured <= Limit ? TEXT("within") : TEXT("OVER"));
    }

    double MB(double Bytes)
    {
        return Bytes / (1024.0 * 1024.0);
    }
}

bool FShadowBakeCostEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShadowBakeCostLocal;
    FScopedCVar Tasks(TEXT("ds.Terrain.BuildTasks"), 2.0f);
    FScopedCVar Uploads(TEXT("ds.Terrain.UploadsPerFrame"), 4.0f);
    const FPlatformMemoryStats Before = FPlatformMemory::GetStats();
    FSkyWorld Test(TEXT("ShadowBakeCostWorld"), 8, EShadows::On);
    Test.Sky->bKeepShadowMapsForTest = true;
    Test.BeginPlay();
    Test.Step(1.0f / 60.0f);
    const double SkyStart = FPlatformTime::Seconds();
    Test.Sky->FlushShadowBakesForTest();
    const double SkySeconds = FPlatformTime::Seconds() - SkyStart;
    FlushRenderingCommands();
    const FPlatformMemoryStats After = FPlatformMemory::GetStats();
    const FSkySystem Here = LocalSystem::Here(Test.World);
    if (!TestTrue(TEXT("the start system has worlds I-V"), Here.Bodies.IsValidIndex(5)))
    {
        return false;
    }
    const FSkyBody& Fourth = Here.Bodies[4];
    const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
    const FGroundFieldRef Field = ShipGround::FromRelief(Fourth.Relief);
    TArray<FString> Report;
    const auto Line = [&](const FString& Text)
    {
        Report.Add(Text);
        AddInfo(Text);
    };

    // -- The system: the sky's own bakes, and their memory as the engine counts it
    {
        int32 Maps = 0;
        double GpuMB = 0.0;
        double CpuMB = 0.0;
        double LevelsMB = 0.0;
        for (const FSkyBody& Body : Here.Bodies)
        {
            UTexture2D* Texture = Test.Sky->GetShadowTexture(Body.Id);
            const FSunShadowMap* Map = Test.Sky->GetShadowMapForTest(Body.Id);
            if (!Texture || !Map)
            {
                continue;
            }
            ++Maps;
            GpuMB += MB(Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips));
            CpuMB += MB(ShipSky::ShadowTextureCpuBytes(*Texture));
            LevelsMB += MB(Map->Bytes());
            TestTrue(FString::Printf(TEXT("%s: the GPU holds mip 0 at the map's width (no LOD bias)"), *Body.Id.ToString()),
                Texture->GetResource() && Texture->GetResource()->GetSizeX() == static_cast<uint32>(Map->Width));
        }
        IConsoleVariable* BakeTasks = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.ShadowBakeTasks"));
        Line(FString::Printf(TEXT("system the sky's bakes: %d worlds on %d tasks, %.2f s from the load to the last texture; GPU %.2f MB resident (the levels are %.2f MB), CPU %.2f MB left; process physical %+.1f MB over the world's start"),
            Maps, BakeTasks ? BakeTasks->GetInt() : 0, SkySeconds, GpuMB, LevelsMB, CpuMB,
            MB(static_cast<double>(After.UsedPhysical) - static_cast<double>(Before.UsedPhysical))));
        Line(FString::Printf(TEXT("system the corpus's worst, %d solid worlds, at this system's %.2f MB a world: %.1f MB on the GPU (reported, not held)"),
            WorstSystemWorlds, GpuMB / FMath::Max(Maps, 1), WorstSystemWorlds * GpuMB / FMath::Max(Maps, 1)));
        Line(Budget(TEXT("system_gpu_mb"), GpuMB, SystemGpuBudgetMB));
        Line(Budget(TEXT("system_cpu_mb"), CpuMB, SystemCpuBudgetMB));
        Line(Budget(TEXT("map_land_ms"), 1e3 * Test.Sky->GetSlowestShadowLandSeconds(), MapLandBudgetMs));
    }

    // -- A jump within the system: the serial moves, the sky rebuilds, nothing is baked
    {
        const int32 Started = Test.Sky->GetShadowBakesStarted();
        Test.Sky->SyncTo(Here, LocalSystem::Serial(Test.World) + 1, false);
        for (int32 Tick = 0; Tick < 10; ++Tick)
        {
            Test.Step(1.0f / 60.0f);
        }
        const int32 Rebakes = Test.Sky->GetShadowBakesStarted() - Started;
        Line(FString::Printf(TEXT("jump within the system: %d bakes started"), Rebakes));
        Line(Budget(TEXT("jump_rebakes"), Rebakes, JumpRebakesBudget));
    }

    // -- The cold cut, and a release with shadowed builds in flight --------------
    const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, 0.0, Test.Ship->GetFlightState().GetUniversePosition(),
                                                                 ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(10.0));
    if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet() && Star))
    {
        return false;
    }
    const FVector Up = (Dusk->Position - Fourth.Position).GetSafeNormal();
    const FVector Sunward = (Star->Position - Fourth.Position).GetSafeNormal();
    const FVector Heading = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
    const auto AtDusk = [&]()
    {
        Test.Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + 150.0),
                             FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
    };
    AtDusk();
    IConsoleVariable* TerrainShadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Terrain.Shadows"));
    IConsoleVariable* BuildTasks = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Terrain.BuildTasks"));
    if (!TestNotNull(TEXT("ds.Terrain.Shadows exists"), TerrainShadows) || !TestNotNull(TEXT("ds.Terrain.BuildTasks exists"), BuildTasks))
    {
        return false;
    }
    const auto ColdCut = [&](int32 On)
    {
        // The switch restarts the ground (Task 4), so the next flush builds the whole cut.
        TerrainShadows->Set(On, ECVF_SetByCode);
        const double Start = FPlatformTime::Seconds();
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        return FPlatformTime::Seconds() - Start;
    };
    const double CutWithout = ColdCut(0);
    const double CutWith = ColdCut(1);
    const int32 Resident = Test.Ground->GetResidentCount();
    Line(FString::Printf(TEXT("cut 1.5m_dusk10 over Baemsekai IV: %d tiles resident on %d tasks; without the shadow %.2f s, with it %.2f s (x%.2f)"),
        Resident, BuildTasks->GetInt(), CutWithout, CutWith, CutWith / FMath::Max(CutWithout, 1e-6)));
    Line(Budget(TEXT("cold_cut_s"), CutWith, ColdCutBudgetSeconds));
    const double TileShadowMB = MB(static_cast<double>(Test.Ground->GetTileShadowBytes()));
    Line(FString::Printf(TEXT("cut vertex shadows: %.2f MB from the arrays, both copies (%.2f MB by the formula for one)"),
        TileShadowMB, MB(Resident * TerrainTile::GridVerts * sizeof(float))));
    Line(Budget(TEXT("tile_shadow_mb"), TileShadowMB, TileShadowBudgetMB));
    {
        // A restart with builds in flight: a sample count the cut was not
        // built at. The release is read as its frame over an ordinary one's
        // median, so the frame's own work is not charged to it.
        FScopedCVar Samples(TEXT("ds.Terrain.ShadowSamples"), 13.0f);
        TArray<double> Ordinary;
        for (int32 Tick = 0; Tick < 11; ++Tick)
        {
            const double Began = FPlatformTime::Seconds();
            Test.Step(1.0f / 60.0f);   // the first restarts under 13 samples; the rest build
            Ordinary.Add(1e3 * (FPlatformTime::Seconds() - Began));
        }
        Ordinary.RemoveAt(0);
        const int32 Building = Test.Ground->GetBuildingCount();
        TerrainShadows->Set(0, ECVF_SetByCode);
        const double Start = FPlatformTime::Seconds();
        Test.Step(1.0f / 60.0f);   // the restart's release, and the frame around it
        const double Ms = 1e3 * (FPlatformTime::Seconds() - Start) - Percentile(Ordinary, 0.5);
        TerrainShadows->Set(1, ECVF_SetByCode);
        Line(FString::Printf(TEXT("release with %d shadowed builds in flight: %.2f ms over an ordinary frame's median (%.2f ms)"),
            Building, Ms, Percentile(Ordinary, 0.5)));
        TestTrue(TEXT("builds were in flight when the ground let go"), Building > 0);
        Line(Budget(TEXT("release_ms"), Ms, ReleaseBudgetMs));
    }
    AtDusk();
    Test.Step(1.0f / 60.0f);
    Test.Ground->FlushBuildsForTest();

    // -- A tile ---------------------------------------------------------------
    const TArray<FTileKey> Drawn = Test.Ground->GetDrawnKeys();
    TArray<FTileKey> Keys;
    for (int32 Index = 0; Index < Drawn.Num() && Keys.Num() < 64; Index += FMath::Max(1, Drawn.Num() / 64))
    {
        Keys.Add(Drawn[Index]);
    }
    const TerrainTile::FTileShadow Ruled = Test.Ground->GetTileShadow();
    const FVector3d Centre = FVector3d(Up);
    const FVector3d Level = (FVector3d(Sunward) - Centre * FVector3d::DotProduct(FVector3d(Sunward), Centre)).GetSafeNormal();
    for (const double Degrees : { 10.0, 3.0, 60.0 })
    {
        TerrainTile::FTileShadow Shadow = Ruled;
        const double Elevation = FMath::DegreesToRadians(Degrees);
        Shadow.Sun.Direction = (Centre * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
        TArray<double> Without;
        TArray<double> With;
        TArray<double> Ratio;
        for (const FTileKey& Key : Keys)
        {
            const double Bare = TerrainTile::Build(*Field, Key).BuildSeconds;
            const double Cast = TerrainTile::Build(*Field, Key, Shadow).BuildSeconds;
            Without.Add(Bare);
            With.Add(Cast);
            Ratio.Add(Cast / FMath::Max(Bare, 1e-9));
        }
        Line(FString::Printf(TEXT("tile sun %.0f: %d keys, without the shadow median %.2f ms p90 %.2f ms, with it median %.2f ms p90 %.2f ms, ratio median x%.2f p90 x%.2f (%d samples)"),
            Degrees, Keys.Num(), 1e3 * Percentile(Without, 0.5), 1e3 * Percentile(Without, 0.9), 1e3 * Percentile(With, 0.5),
            1e3 * Percentile(With, 0.9), Percentile(Ratio, 0.5), Percentile(Ratio, 0.9), Shadow.Samples));
        if (Degrees == 10.0)
        {
            Line(Budget(TEXT("tile_ratio"), Percentile(Ratio, 0.5), TileRatioBudget));
        }
    }

    // -- A world's map: the start system's five, and the corpus's two extremes
    const int32 Columns = ShipSky::ShadowMapWidth();
    const SunShadow::FSunLight FourthLight = SkyProjection::SunLightOf(Here, 4);
    struct FWorld
    {
        FString Name;
        FWorldReliefParams Relief;
        SunShadow::FSunLight Sun;
    };
    TArray<FWorld> Worlds;
    for (int32 Index = 1; Index <= 5; ++Index)
    {
        if (Here.Bodies[Index].Ground == EGround::Solid)
        {
            Worlds.Add({ Here.Bodies[Index].Id.ToString(), Here.Bodies[Index].Relief, SkyProjection::SunLightOf(Here, Index) });
        }
    }
    const auto Extreme = [&](const TCHAR* Name, double RadiusEarth, double PeakKm)
    {
        FWorldReliefParams Relief = Fourth.Relief;
        Relief.RadiusCm = RadiusEarth * EarthRadiusCm;
        Relief.PeakCm = PeakKm * 1.0e5;
        Relief.Cratering = 1.0;
        Worlds.Add({ FString::Printf(TEXT("%s-like (the corpus's, at Cratering 1)"), Name), Relief, FourthLight });
    };
    Extreme(TEXT("Baiti I"), 0.443141, 9.74015);
    Extreme(TEXT("Tishras I"), 2.12699, 2.5735);
    double Slowest = 0.0;
    double Largest = 0.0;
    for (const FWorld& World : Worlds)
    {
        const FReliefGround Ground(World.Relief);
        const double Start = FPlatformTime::Seconds();
        const FSunShadowMap Map = SunShadowMap::Bake(Ground, World.Sun, SunShadow::SteepestSlope(World.Relief), Columns);
        const double Seconds = FPlatformTime::Seconds() - Start;
        Slowest = FMath::Max(Slowest, Seconds);
        Largest = FMath::Max(Largest, MB(Map.Bytes()));
        TestTrue(FString::Printf(TEXT("%s's map baked"), *World.Name), Map.LevelCount() > 0);
        Line(FString::Printf(TEXT("map %s: %d x %d, %d levels, %.2f s on one thread, %.2f MB, psi_lo %.2f deg"),
            *World.Name, Map.Width, Map.Rows, Map.LevelCount(), Seconds, MB(Map.Bytes()), FMath::RadiansToDegrees(Map.PsiLo)));
    }
    Line(Budget(TEXT("world_bake_s"), Slowest, WorldBakeBudgetSeconds));
    Line(Budget(TEXT("world_map_mb"), Largest, WorldMapBudgetMB));

    // -- The flight, with the shadow on ------------------------------------------
    {
        // Fresh ground: far enough off that the ground lets go of everything.
        const FVector Away = (Test.Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
        Test.Ship->PlaceShip(Fourth.Position + Away * (Fourth.Radius + 5.0e8), FRotationMatrix::MakeFromX(-Away).ToQuat());
        Test.Step(1.0f / 60.0f);
        const GroundKeepsUpScenario::FResult R = GroundKeepsUpScenario::Fly(Test);
        TestTrue(TEXT("the flight was flown"), R.bValid);
        Line(FString::Printf(TEXT("flight GroundKeepsUp's with the shadow on: %d frames under the drive floor, %d under 1 km; %d without drawn ground, %d not drawing the body, worst drawn gap %.2f cm (allowed %.2f)"),
            R.UnderFloor, R.Low, R.Missing, R.NotDrawing, R.Worst, R.Tolerance));
        Line(Budget(TEXT("flight_missing"), R.Missing, 0.0));
        Line(Budget(TEXT("flight_worst_cm"), R.Worst, R.Tolerance));
        Line(Budget(TEXT("flight_not_drawing"), R.NotDrawing, 0.0));
    }
    {
        // The handover's wait: from 49 km over ground nothing was built for.
        const FVector Side = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
        const FVector Fresh = (Up * FMath::Cos(0.8) + Side * FMath::Sin(0.8)).GetSafeNormal();
        Test.Ship->PlaceShip(Fourth.Position + Fresh * (Fourth.Radius + 5.0e8), FRotationMatrix::MakeFromX(-Fresh).ToQuat());
        Test.Step(1.0f / 60.0f);
        Test.Ship->PlaceShip(Fourth.Position + Fresh * (Fourth.Radius + 4.9e6), FRotationMatrix::MakeFromX(-Fresh).ToQuat());
        const double Start = FPlatformTime::Seconds();
        double Waited = -1.0;
        while (FPlatformTime::Seconds() - Start < 60.0)
        {
            const double Began = FPlatformTime::Seconds();
            Test.Step(1.0f / 60.0f);
            if (Test.Ground->IsDrawingBody())
            {
                Waited = FPlatformTime::Seconds() - Start;
                break;
            }
            while (FPlatformTime::Seconds() - Began < 1.0 / 60.0)
            {
                FPlatformProcess::Sleep(0.001f);
            }
        }
        Line(FString::Printf(TEXT("coarse the ground drew the body %.2f s after arriving 49 km over fresh ground, shadow on (-1: not in 60 s)"), Waited));
        Line(Budget(TEXT("coarse_s"), Waited < 0.0 ? 60.0 : Waited, CoarseBudgetSeconds));
    }

    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ShadowBakeCost");
    IFileManager::Get().MakeDirectory(*Dir, true);
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return true;
}

#endif
```

`GroundKeepsUpScenario::Fly` places the ship at IV's drive floor itself, over "the ship's own side of the world", as `GroundKeepsUp` always did. The placement 5,000 km out before it only makes the ground let go of the dusk cut first, so the flight starts from what play would have.

- [ ] **Step 3: Run it**, on a quiet machine: no other build, test or render running.

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.GroundKeepsUp && Tools/eyes.sh Eyes.ShadowBakeCost; cat Saved/Eyes/ShadowBakeCost/report.txt`

Expected: `GroundKeepsUp` passes as before. `Eyes.ShadowBakeCost` reports `passed: 1` (a run of about four minutes), with the `system`, `jump`, `cut`, `release`, three `tile`, seven `map`, `flight` and `coarse` lines, and fourteen `budget` lines. Planning expects:
- the cold cut at about 3 s without the shadow and about 27 s with it;
- a release well under 2 ms, since nothing waits;
- a tile about 8x at 10 degrees, less at 3 (many vertices exit hidden), about 1x at 60 (the day exit);
- each map about 8-15 s on one thread and about 12 MB, the Baiti I-like the most rows;
- the system at about 60 MB on the GPU and 0 left on the CPU;
- the jump at 0 bakes;
- the flight: in doubt. At about 40 tiles a second the finest levels come late, and whether the drawn gap stays within GearClearance / 10 is the question this measure exists to answer.

A number far from its prediction is a finding to report, not a failure. If the process's physical memory rose by about the GPU figure again, the CPU copy is still held somewhere (`ShadowTextureCpuBytes` says 0): report it with Task 7b's memory options.

- [ ] **Step 4: The verdict.**
  - **Every budget line reads `within`:** write the report's budget lines and the `system`, `cut`, `tile sun 10`, `map`, `flight` and `coarse` lines into this file's header comment, as one line each after `Nothing is asserted...`, headed `Measured (2026-09-28, RTX 4070 Ti SUPER, Ryzen 5 7600X):`. Commit (Step 5), and go on to Task 8.
  - **Any line reads `OVER`:** commit the measurement (Step 5) and go to Task 7b. Do not start Task 8.

- [ ] **Step 5: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Tests/GroundKeepsUpScenario.h Source/DeepSpace/Tests/GroundKeepsUpTest.cpp \
  Source/DeepSpace/Tests/Eyes/ShadowBakeCostEyesTest.cpp && \
git commit -qm "test(eyes): the baked cast shadow's cost -- per tile, per world, per system, in flight, and its memory, against their budgets

GroundKeepsUp's flight moves into GroundKeepsUpScenario.h, unchanged, so
Eyes.ShadowBakeCost flies it with the tile shadows on.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

No mutation proves a measurement. The budget lines' `OVER` wording is what Task 7b reads, and a run with `ColdCutBudgetSeconds` set to 0.001 must print `OVER`. Check it once by hand (edit, build, run, restore) and record in the task report that it did.

---

## Task 7b (only if a budget is over): a stop-and-report point, not an executable task

Reached from Task 7. Stop, and put the report's lines to the developer with the options that match the budget that failed. Do not tune past a budget without the ruling.

**REACHED, 2026-09-28 -- awaiting the developer's ruling. Task 8 is not started.** `Eyes.ShadowBakeCost` (3742b25, two runs) read two budgets `OVER`: `flight_worst_cm` 25.99 and 25.22 against 15.00, and `map_land_ms` 9.71 and 9.35 against 4.00; every other line `within` (`cold_cut_s` 13.26 s, `tile_ratio` x9.44-9.50, `coarse_s` 9.61-9.65 s, `world_bake_s` 10.08 s, `system_gpu_mb` 63.92). A review then found the headless suite had hidden the first: `FSkyWorld` turns both shadow switches off, so `GroundKeepsUp` flew a configuration the game does not ship. Since then (this branch):
- `GroundKeepsUp`, the handover, the landing playtests and `Loop.Jump` fly `EShadows::On`. `GroundKeepsUp` starts over the opening's side of IV, under a 62.6-degree sun, over the day exit: 14.40 cm with the shadow or without, within.
- **`DeepSpace.Surface.GroundKeepsUpAtDusk`** flies the same flight from IV's 10-degree dusk, asserted, and is **red**. Measured headless (worst drawn gap, cm, against 15.00):

  | build tasks | no shadow | shadow | shadow, `DetailSum` skipping faded bands |
  |---|---|---|---|
  | 2 (the default) | 15.03 | 25.22 | 25.22 |
  | 3 | -- | -- | 15.03 |
  | 4 | 7.19 | 15.03 | 9.52 |

  The gap is the ground under the ship lagging its build queue, not the shadow's arithmetic: it falls with workers. **At 2 tasks it is 0.03 cm over even with no shadow at all**, so the dusk flight was marginal before the shadow existed; the opening side had hidden that too.
- `FWorldRelief::DetailSum` now skips a band its footprint has faded whole (this section's "First, profile" item). It changes no value (`WorldRelief.KnownValues` exact; its mutant killed) and is committed; with 4 tasks it took the gap from 15.03 to 9.52, and at 2 it moves nothing.
- The day exit by footprint (2(e)) is not built: at a 10-degree sun only tiles whose footprint has faded all but about two of the twenty bands could exit, which are the handful of coarsest levels, not the fine tiles the gap waits on. It stays on offer.

- **Re-measured after fc829a7 (2026-09-28), which supersedes the table and the options' numbers
  above.** The landing plan's Task 39 made the crater kernel visit the 8 lattice corners that can
  reach instead of 27 (every value unchanged), and the C++ tile heights go through the same kernel,
  so every build is cheaper. On a quiet machine, `GroundKeepsUpAtDusk`'s worst drawn gap, cm,
  against 15.00:

  | build tasks | no shadow | shadow |
  |---|---|---|
  | 2 (the default) | 15.03 (3 runs) | 15.03 (5 runs) |
  | 3 | -- | 7.19 (2 runs) |
  | 4 | -- | 7.19 (2 runs) |

  `Eyes.ShadowBakeCost`, run once: `flight_worst_cm` 15.03 OVER (was 25.99 and 25.22), and
  `map_land_ms` 10.05 OVER (was 9.71 and 9.35: the kernel does not touch the upload); every other
  line within -- `cold_cut_s` 4.40 (was 13.26), `tile_ratio` x7.75 (was x9.44-9.50), `coarse_s`
  5.12 (was 9.61-9.65), `world_bake_s` 4.17 (was 10.08), `system_gpu_mb` 63.92.
  How far to read this: the gap moves in steps (7.19, 9.52, 14.40, 15.03, 25.22 are every value
  it has read), and the flight is paced to the wall clock, so a loaded machine can read a step
  higher; nothing here measured that. At 2 tasks the shadow no longer moves the gap a step, which
  is not the same as costing nothing. The gap left at 2 tasks is the one the flight had with no
  shadow before the shadow existed.

The options to put to the developer, with the re-measured numbers (the figures in the next
sentence are from before fc829a7): `ds.Terrain.BuildTasks` 3 now reaches 7.19 with the shadow,
where it read 15.03 before, with the maps' 2 bake tasks beside it as now. As first put:
`ds.Terrain.BuildTasks` 3 alone does not reach 15.00 (15.03), 4 does (9.52) but is the machine's whole cap with the maps' 2 bake tasks; every other vertex, interpolated (a coarsening, ruled); 8 samples (a quality NO-GO unless relaxed); a larger tolerance than GearClearance / 10 at dusk (it was already 15.03 with no shadow); or prioritising the chain of tiles under the ship over the prefetch. For `map_land_ms`: the RHI upload on the render thread, or accept a ~9.5 ms hitch once a world. `GroundKeepsUpAtDusk` stays red until the ruling; `feat/landing-b-t` does not merge into `feat/landing-b` while it is.

- **`cold_cut_s`, `tile_ratio`, `coarse_s` or a `flight_*` budget over:** Task 0's second question, with measured numbers now. The first remedy to propose is the day exit by footprint (Task 0, 2(e)).
  - It is `SunShadow::SteepestSlope(Params, FootprintCm)`: the sampled steepest slope of only the bands not yet faded at the tile's spacing, each band's sampled gradient measured as `.SteepestSlope` measures the sum.
  - It changes no value: a vertex over its exit is whole either way. `.Exits` must hold at every footprint it is given.
  - It needs no ruling, only its measurement.
  - The other options follow.
  - `ds.Terrain.BuildTasks` 3;
  - every other vertex, interpolated (a coarsening: it needs the ruling);
  - `ds.Terrain.ShadowSamples` 8, which `.AgainstProfile` measured as over its 5-degree bound (a quality NO-GO unless the developer relaxes it);
  - or a larger budget.
  - First, profile `FWorldRelief::Height`, whose crater sum is most of a height's cost. `DetailSum` evaluates every band, even one its footprint has faded to 0. Skipping those (`if (FootprintD * Frequencies[Band] >= 1.0) continue;`) changes no value, since the band's scale is exactly 0 there, and saved about 25% of a coarse read in planning's harness. Such a change must keep `DeepSpace.Surface.WorldRelief.KnownValues` and `Eyes.WorldReliefParity` exact, and is proposed with its measurement.
- **`world_bake_s` over:** a narrower `ds.Sky.ShadowMapWidth` (2048 is about 4x faster), or more bake tasks. Or accept it: a world's map lands this many seconds after the system loads, and until then it draws unshadowed.
- **`world_map_mb`, `system_gpu_mb` or `tile_shadow_mb` over:**
  - a narrower width;
  - 8-bit texels (half the memory, 1/255 steps);
  - baking only the worlds within some reach of the ship (a new ruling: it is no longer "baked when the system loads");
  - dropping one of the two kept copies of a tile's `SunVisible`: the resident tile's, or `UTerrainTileComponent`'s, which exists only for proxy recreation.
- **`system_cpu_mb` over 0:** the CPU copy was not discarded. If `RemoveBulkData` is refused on a transient mip, say so. The option is then to upload the mips through the RHI directly (`RHIUpdateTexture2D` into a texture created with no bulk data), which is a larger change and needs the go-ahead.
- **`map_land_ms` over:** the same RHI upload, on the render thread, so the game thread only enqueues it. Or accept a hitch of that size once a world, up to a world's bake time after the system loads.
- **`release_ms` over:** a bug, not a tune. Something still waits on a build: look for a `Wait()` outside `*ForTest`.
- **`jump_rebakes` over 0:** a bug, not a tune. A key changed on a jump that moved neither input. Look at what `SameShadowKey` compares: the light is body-to-star and must not move with the ship.

When the ruling comes back, write it into the spec under the ruling's bullet, amend Task 7's budgets or Tasks 4/5's defaults, re-run Task 7, and go on.

**RULED, 2026-09-28, and built** (the spec's "The cast-shadow plan's Task 0 and Task 7b, ruled"):
- **Three workers.** `ds.Terrain.BuildTasks` defaults to 3 (5e3bb7b); every flight test flies the game's default. `GroundKeepsUpAtDusk` went green at 7.19 cm (three runs). But three workers turned **`GroundKeepsUp` red, deterministically, at 792.45 cm** -- the streaming, not the shadow. `AWorldGround::Launch` built only the prefetch and the leaves; `Balance` splits a leaf the ship has just reached into children the frame it enters the cut, so that node became an ancestor that was never built, and `Resolve` drew the nearest resident ancestor whole: a level-2 tile over the ship, 792 m off the ground, for two frames at 500 m; later the chain stuck at level 12 at 30 m (163 cm). Launch now builds every needed key, ancestors too, coarse first; and `BoundsOf` remembers each child range for the ground's life, since forgetting it made a horizon tile that culls its children once resident flip in and out of residency frame to frame. After both, four runs each on 3 tasks: **`GroundKeepsUp` 10.21 cm, `GroundKeepsUpAtDusk` 12.84 cm**, against 15.00 (at 2 tasks they were 14.40 and 15.03). Both fixes' mutants killed. With the maps' 2 bake tasks beside them while a system loads, up to 5 background tasks run at once -- over the plan's "4, the machine's cap"; the ruling set the tile builds, and the bake tasks were left at 2.
- **The map uploaded in pieces.** A landed map's texture is a `UTexture2DDynamic` created empty on the render thread; each frame hands the render thread at most `ds.Sky.ShadowUploadKB` (512) of its rows as one render command, level 0 first; a world reads the map only after its last piece, then fades in; the CPU copy goes once a fence says every piece has run. `DeepSpace.Sky.ShadowParameters` sees a map go up over several frames and no world read it early (mutant killed); `.ShadowUploadPieces` holds the pieces pure.
- **The 128 MB per-system cap.** `DeepSpace.Sky.ShadowSystemCap` holds it pure (sixteen IV-like worlds: 121.4 MB after it; mutant killed). The first `Eyes.ShadowBakeCost` run capped the levels' own bytes and the GPU held more (Trabo's twelve: 142 MB against 127 MB of levels, a texture's alignment and mip tail); the sky now caps `ShipSky::ShadowTextureBytes`, `RHICalcTexturePlatformSize` for the texture it makes.
- **The corpus's worst system is Trabo, 12 solid worlds**, in today's `Saved/procgen_corpus.tsv` (10,000 systems, 3.67 solid worlds on average) -- not the 16 planning read. `Eyes.ShadowBakeCost` bakes Trabo through the sky, and Trabo with four of its worlds again (16).
- **`Eyes.ShadowBakeCost` after the rulings** (2026-09-29, RTX 4070 Ti SUPER, Ryzen 5 7600X, `ds.Terrain.BuildTasks` 3):
  - `system` the start system's five on 2 tasks, 10.06 s to the last texture; GPU 63.92 MB (levels 58.23), CPU 0.00 MB left; every world at 4096.
  - `upload` in pieces of 512 KB: the slowest frame's landing 0.159 ms on the game thread, 0.072 ms on the render thread.
  - `system worst` Trabo, 12 worlds, 12.94 s: GPU 123.99 MB (levels 109.31), Trabo II, III and V at 2048, the rest 4096. `system worst16`: GPU 120.33 MB (levels 103.52), I-V and I-IV again at 2048.
  - `cut` 1474 tiles resident (951 before the ancestors were built) on 3 tasks: 0.80 s without the shadow, 5.27 s with it.
  - `tile sun 10` ratio median x7.70; `map` slowest 4.16 s (Baemsekai I), largest 12.57 MB; `flight` 12.84 cm; `coarse` 2.89 s.
  - Budget lines: `system_gpu_mb` 63.919 / 128 within; `system_cpu_mb` 0 / 0 within; `map_land_ms` 0.159 / 4 within; `map_upload_rt_ms` 0.072 / 4 within; `jump_rebakes` 0 / 0 within; `cold_cut_s` 5.266 / 30 within; **`tile_shadow_mb` 12.247 / 12 OVER**; `release_ms` -0.017 / 2 within; `tile_ratio` 7.699 / 10 within; `world_bake_s` 4.160 / 15 within; `world_map_mb` 12.568 / 16 within; `flight_missing` 0 / 0 within; `flight_worst_cm` 12.837 / 15 within; `flight_not_drawing` 0 / 0 within; `coarse_s` 2.894 / 10 within; `system_gpu_mb_worst` 123.988 / 128 within; `system_gpu_mb_worst16` 120.328 / 128 within; `map_land_ms_all` 0.159 / 4 within; **`map_upload_rt_ms_all` 4.336 / 4 OVER**.
  - **Two OVER, for the developer.** `tile_shadow_mb` 12.25 MB: the cut now keeps its ancestors resident (1474 tiles, not 951), both copies of each tile's shadow counted; the options are the budget, or 16-bit `SunVisible` (half), or dropping the component's kept copy. `map_upload_rt_ms_all` 4.34 ms: one render command, once, while the sixteen-world test system's maps landed back to back in a flush (1024 KB pieces read 4.59 ms there; the start system's, in the same flush path, 0.07 ms); not diagnosed. This render-thread line is this work's own addition beside the ruled game-thread `map_land_ms`, which is within everywhere.

---

## Task 8: parity -- the map on the GPU, and the handover at dusk

**Owner:** T. **Depends on:** Task 7's verdict, GO.

**Files:**
- Modify: `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp`: the shadow-map leg, its SUMMARY line, and its verdict line in the header
- Modify: `Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp`: the dusk leg

**Interfaces:**
- Consumes: `M_SkyShadowProbe`, `SkyMaterial::{ShadowProbePath, ShadowMap, ShadowFrameX, ShadowFrameZ, ProbeFootprint}`, `ShipSky::{MakeShadowTexture, ShadowFrameX, ShadowFrameZ}`, `AShipSky::FlushShadowBakesForTest` (Task 6); `SunShadowMap::{Shape, Bake, Sample, SampleF32}` (Task 5); `SkyProjection::SunLightOf` (Task 3); `SunShadow::SteepestSlope`; the parity test's `Draw`, `Selecting`, `EPass`, `FGap`, `IsFinite`, `FloorRuleFactor`, `Side`, `HomeSky`, `Fourth`, `Probe`, `Target`, `Report`.
- Produces: report lines `Baemsekai IV's shadow map, <place>, footprint <f>: ...`, and `SUMMARY shadow map C++-vs-GPU <x>, float-vs-double <y>`; and a second line in `Saved/Eyes/HandoverParity/report.txt`.

**The tolerance, decided now:** the measured floor as a rule. The texels are the same 16-bit integers on both sides, and the GPU's `Load` of a UNORM16 texel is that integer over 65535. What is left is the lookup's own arithmetic in float: the frame's dot products, `asin`, `atan2`, `log2`, and the taps' weights. So at each footprint and each place the GPU is held to the larger of 1e-3 and 1.25 x the float mirror's (`SampleF32`) distance from double, measured in the same run at the same D. Every pixel is held: the lookup is continuous in D, the seam included, and its only steps are the texels' own values. The one exception is the night edge at `PsiLo`, where float and double can fall on either side. There the step is row 0's value, which is nearly 0 on a real map, since the rows just above `PsiLo` are almost all dark. A FLOAT FLOOR verdict escalates.

**The handover, decided now:** at 49.9 km over IV under a 3-degree dusk, turned about the light until the map shades the view's central quarter (its mean at most 0.9, read from the kept map before any frame is shot), the ground's frame must be the orbit's to 1e-3 of its mean with the shadow on. The shadow must reach both frames: each is darker with it than without, by the same share to 1e-3. And the seam's per-pixel p99 gap may grow at most 3x with the shadow on. At 49.9 km the morph is about 3e-5, so the ground's shadow is the map to that.

- [ ] **Step 1: The map's leg.** In `WorldReliefParityTest.cpp`, add the includes `#include "Engine/Texture2D.h"`, `#include "Sky/SkyProjection.h"`, `#include "Surface/GroundField.h"`, `#include "Surface/SunShadow.h"` and `#include "Surface/SunShadowMap.h"`. Before `Report.Add(FString::Printf(TEXT("SUMMARY ground normal C++-vs-GPU %.2e"), WorstGroundNormal));`, add:

```cpp
    // -- The cast shadow's map (the developer's ruling on slice (b)'s build:
    // baked, not marched) ------------------------------------------------------
    // M_SkyShadowProbe draws the map's lookup -- the shared file's
    // WR_ShadowMapCoord, WR_ShadowTapsAt and WR_Bilinear over the texture's own
    // texels -- over the probe patch. The map is Baemsekai IV's, baked here at
    // 1,024 columns under two lights 3 degrees over the patch's centre: one
    // turned so the map's frame puts the centre at azimuth 0 (the patch across
    // the terminator), one at +-pi (the patch across the map's seam). The C++
    // is SunShadowMap::Sample at the very D the GPU drew, at five footprints
    // from far finer than a texel (6.1e-3 rad) to one that reads level 2.
    // Held by the measured floor as a rule: 1e-3, or 1.25 x the float
    // mirror's distance from double at that footprint, whichever is larger.
    double WorstShadow = 0.0;
    double WorstShadowFloat = 0.0;
    {
        UMaterial* ShadowShared = LoadObject<UMaterial>(nullptr, SkyMaterial::ShadowProbePath);
        if (!TestNotNull(TEXT("M_SkyShadowProbe is built (Tools/setup_sky_materials.py)"), ShadowShared))
        {
            return false;
        }
        UMaterialInstanceDynamic* ShadowProbe = UMaterialInstanceDynamic::Create(ShadowShared, Test.World);
        const FLinearColor NoBias(0.0f, 0.0f, 0.0f, 0.0f);
        const TArray<FVector3d> Directions = Draw(Test.World, Target, Probe, Selecting(EPass::Direction), NoBias);
        const FVector3d Centre = Directions[(Side / 2) * Side + Side / 2].GetSafeNormal();
        const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Centre).GetSafeNormal();
        const FVector3d North = FVector3d::CrossProduct(Centre, East);
        const FReliefGround Relief(Fourth.Relief);
        const SunShadow::FSunLight Home = SkyProjection::SunLightOf(HomeSky, 4);
        const double Steepest = SunShadow::SteepestSlope(Fourth.Relief);
        struct FPlace
        {
            const TCHAR* Name;
            double Azimuth;
        };
        for (const FPlace& Place : { FPlace{ TEXT("across the terminator"), 0.0 }, FPlace{ TEXT("across the seam"), UE_DOUBLE_PI } })
        {
            // Turn the light about the centre until the map's frame puts the
            // centre at the azimuth asked.
            SunShadow::FSunLight Sun = Home;
            double Nearest = TNumericLimits<double>::Max();
            const double Elevation = FMath::DegreesToRadians(3.0);
            for (int32 Turn = 0; Turn < 3600; ++Turn)
            {
                const double Around = Turn * 2.0 * UE_DOUBLE_PI / 3600.0;
                SunShadow::FSunLight Trial = Home;
                Trial.Direction = (Centre * FMath::Sin(Elevation)
                    + (East * FMath::Cos(Around) + North * FMath::Sin(Around)) * FMath::Cos(Elevation)).GetSafeNormal();
                const FSunShadowMap Shape = SunShadowMap::Shape(Relief, Trial, 1024);
                const double Phi = FMath::Atan2(FVector3d::DotProduct(Centre, Shape.FrameY), FVector3d::DotProduct(Centre, Shape.FrameX));
                const double Off = FMath::Abs(FMath::Fmod(Phi - Place.Azimuth + 3.0 * UE_DOUBLE_PI, 2.0 * UE_DOUBLE_PI) - UE_DOUBLE_PI);
                if (Off < Nearest)
                {
                    Nearest = Off;
                    Sun = Trial;
                }
            }
            TestTrue(FString::Printf(TEXT("a light puts the patch %s (%.3f deg off)"), Place.Name, FMath::RadiansToDegrees(Nearest)),
                Nearest < FMath::DegreesToRadians(0.5));
            const FSunShadowMap Map = SunShadowMap::Bake(Relief, Sun, Steepest, 1024);
            UTexture2D* Texture = ShipSky::MakeShadowTexture(Map, TEXT("Parity"));
            if (!TestNotNull(TEXT("the map uploads"), Texture))
            {
                return false;
            }
            FlushRenderingCommands();
            ShadowProbe->SetTextureParameterValue(SkyMaterial::ShadowMap, Texture);
            ShadowProbe->SetVectorParameterValue(SkyMaterial::ShadowFrameX, ShipSky::ShadowFrameX(Map));
            ShadowProbe->SetVectorParameterValue(SkyMaterial::ShadowFrameZ, ShipSky::ShadowFrameZ(Map));
            for (const double FootprintD : { 1.0e-5, 1.0e-4, 1.5e-3, 6.0e-3, 2.4e-2 })
            {
                const float Footprint = static_cast<float>(FootprintD);
                ShadowProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
                const TArray<FVector3d> Drawn = Draw(Test.World, Target, ShadowProbe, NoBias, NoBias);
                double Gap = 0.0;
                double FloatGap = 0.0;
                int32 NotFinite = 0;
                int32 Shaded = 0;
                for (int32 Index = 0; Index < Side * Side; ++Index)
                {
                    const FVector3d& D = Directions[Index];
                    const FVector3d& Pixel = Drawn[Index];
                    if (!IsFinite(D) || !IsFinite(Pixel))
                    {
                        ++NotFinite;
                        continue;
                    }
                    const double Held = SunShadowMap::Sample(Map, D, static_cast<double>(Footprint));
                    const double Float = SunShadowMap::SampleF32(Map, FVector3f(D), Footprint);
                    Gap = FGap::Wider(Gap, FMath::Abs(Held - Pixel.X));
                    FloatGap = FGap::Wider(FloatGap, FMath::Abs(Held - Float));
                    Shaded += Held < 0.5 ? 1 : 0;
                }
                const double HeldTo = FMath::Max(1.0e-3, FloorRuleFactor * FloatGap);
                const FString At = FString::Printf(TEXT("Baemsekai IV's shadow map, %s, footprint %.1e"), Place.Name, FootprintD);
                TestEqual(At + TEXT(": every pixel the GPU drew is finite"), NotFinite, 0);
                TestTrue(FString::Printf(TEXT("%s: SunShadowMap::Sample computes what the GPU drew, held to %.1e (the float mirror %.2e from double): %.2e"),
                    *At, HeldTo, FloatGap, Gap), Gap <= HeldTo);
                WorstShadow = FMath::Max(WorstShadow, Gap);
                WorstShadowFloat = FMath::Max(WorstShadowFloat, FloatGap);
                Report.Add(FString::Printf(TEXT("%s: %d compared, %.1f%% under half, %d not finite"), *At, Side * Side - NotFinite,
                    100.0 * Shaded / (Side * Side), NotFinite));
                Report.Add(FString::Printf(TEXT("  shadow map C++ vs GPU %.2e, held to %.1e; float mirror vs double %.2e"), Gap, HeldTo, FloatGap));
            }
        }
    }
    Report.Add(FString::Printf(TEXT("SUMMARY shadow map C++-vs-GPU %.2e, float-vs-double %.2e"), WorstShadow, WorstShadowFloat));
```

- [ ] **Step 2: Run it.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && Tools/eyes.sh Eyes.WorldReliefParity; grep -A1 "shadow map" Saved/Eyes/WorldReliefParity/report.txt | head -30; grep -c "error X\|Shader compile failed\|Failed to compile" Saved/Logs/DeepSpace.log`

Expected: PASS, with 0 shader-compile errors and ten shadow-map blocks (two places, five footprints). This is the first run that compiles the shadow's Custom node. **A compile error here is the HLSL half of the shared file, or `SHADOW_CODE`**: fix it there (both compilers must still take the file), re-author the materials, and re-run. The existing legs are unchanged. The possible verdicts:
  - **PASS:** go on.
  - **Over its allowance at a few pixels of the finest footprint only, the float mirror close to it:** a FLOAT FLOOR verdict (the GPU's `atan2` or `log2` beyond the C++ float build's). Stop and report the gaps; the tolerance is not loosened.
  - **Over broadly, or at the seam's place only:** a port bug. Look first at the frame's `w` channels (PsiLo and Step reaching the node), the pin order, and the taps' wrap.

- [ ] **Step 3: The handover at dusk.** In `HandoverParityEyesTest.cpp`, add the includes `#include "HAL/IConsoleManager.h"`, `#include "Sky/ShipSky.h"` (if absent) and `#include "Surface/SunShadowMap.h"`. Make the world `FSkyWorld Test(TEXT("HandoverParityWorld"), 8, EShadows::On);`, set `Test.Sky->bKeepShadowMapsForTest = true;` before `Test.BeginPlay();`, and after `Test.BeginPlay();` add:

```cpp
    // Every world's cast-shadow map is baked before any frame is judged.
    Test.Step(1.0f / 60.0f);
    Test.Sky->FlushShadowBakesForTest();
```

Then, before `const FString Dir = ...`, add:

```cpp
    // The cast shadow at the handover (the developer's ruling on slice (b)'s
    // build): at 49.9 km over Baemsekai IV under a 3-degree dusk the ground's
    // frame is still the orbit's to 1e-3, and the shadow reaches both -- with
    // ds.Sky.Shadows 0 each is brighter, by the same share. At 49.9 km the
    // morph is about 3e-5, so the ground's shadow is the orbit's map to that.
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    if (!TestNotNull(TEXT("ds.Sky.Shadows exists"), Shadows))
    {
        return false;
    }
    const float ShadowsWere = Shadows->GetFloat();
    const auto CentralMean = [](const TArray<FLinearColor>& Pixels)
    {
        double Sum = 0.0;
        int32 Count = 0;
        for (int32 Y = Size / 4; Y < 3 * Size / 4; ++Y)
        {
            for (int32 X = Size / 4; X < 3 * Size / 4; ++X)
            {
                const FLinearColor& P = Pixels[Y * Size + X];
                Sum += (P.R + P.G + P.B) / 3.0;
                ++Count;
            }
        }
        return Sum / Count;
    };
    // The seam, not the mean: the two meshes can put a pixel in slightly
    // different places (this test's header), and a shadow's edge moves more
    // pixels than shading does. So the per-pixel p99 gap is taken with the
    // shadow and without, and the shadow may not grow it past 3x.
    const auto GapP99 = [](const TArray<FLinearColor>& A, const TArray<FLinearColor>& B)
    {
        TArray<double> Gaps;
        for (int32 Y = Size / 4; Y < 3 * Size / 4; ++Y)
        {
            for (int32 X = Size / 4; X < 3 * Size / 4; ++X)
            {
                const FLinearColor& P = A[Y * Size + X];
                const FLinearColor& Q = B[Y * Size + X];
                Gaps.Add(FMath::Abs((P.R + P.G + P.B) / 3.0 - (Q.R + Q.G + Q.B) / 3.0));
            }
        }
        Gaps.Sort();
        return Gaps[FMath::FloorToInt32(0.99 * (Gaps.Num() - 1))];
    };
    const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, Index, 4.99e6, Ship->GetFlightState().GetUniversePosition(),
                                                                 ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(3.0));
    if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet()))
    {
        return false;
    }
    // The view must hold shade before the shadow can be said to reach it.
    // The capture is 20 degrees straight down from 49.9 km, so its central
    // quarter is about 4.4 km of ground either way: about one 8.8 km texel.
    // Where that texel falls would decide the leg. So the map itself is read
    // first -- SunShadowMap::Sample over the central quarter at a pixel's
    // footprint -- and the placement is turned about the light, which keeps
    // the star 3 degrees up at the nadir, until the map shades that ground
    // by at least a tenth.
    const FSunShadowMap* Map = Test.Sky->GetShadowMapForTest(Fourth.Id);
    if (!TestNotNull(TEXT("Baemsekai IV's map is baked and kept"), Map))
    {
        return false;
    }
    const auto ViewMean = [&](const FUniversePosition& Position)
    {
        const FVector Offset = Position - Fourth.Position;
        const FVector3d Nadir = FVector3d(Offset.GetSafeNormal());
        const double Altitude = Offset.Size() - Fourth.Radius;
        const double TanHalf = FMath::Tan(FMath::DegreesToRadians(0.5 * Capture->FOVAngle));
        const double Half = Altitude * 0.5 * TanHalf / Fourth.Radius;     // the central quarter's half-width, rad
        const double Pixel = 2.0 * TanHalf * Altitude / Size / Fourth.Radius;   // one pixel's footprint, D units
        const FVector3d East = FVector3d::CrossProduct(FVector3d::UnitZ(), Nadir).GetSafeNormal();
        const FVector3d North = FVector3d::CrossProduct(Nadir, East);
        double Sum = 0.0;
        int32 Count = 0;
        for (int32 Y = -8; Y <= 8; ++Y)
        {
            for (int32 X = -8; X <= 8; ++X)
            {
                Sum += SunShadowMap::Sample(*Map, (Nadir + (East * X + North * Y) * (Half / 8.0)).GetSafeNormal(), Pixel);
                ++Count;
            }
        }
        return Sum / Count;
    };
    FNavPlacement Placed = *Dusk;
    double Shade = ViewMean(Placed.Position);
    for (int32 Turn = 1; Turn <= 720 && Shade > 0.9; ++Turn)
    {
        // 0.05 degrees a turn, about 4.7 km on IV: half a texel.
        const FQuat About(FVector(Map->FrameZ), Turn * FMath::DegreesToRadians(0.05));
        FNavPlacement Trial = *Dusk;
        Trial.Position = Fourth.Position + About.RotateVector(Dusk->Position - Fourth.Position);
        Trial.Orientation = About * Dusk->Orientation;
        const double Seen = ViewMean(Trial.Position);
        if (Seen < Shade)
        {
            Shade = Seen;
            Placed = Trial;
        }
    }
    if (!TestTrue(FString::Printf(TEXT("the view holds shade: the map's mean over the central quarter is %.3f, at most 0.9"), Shade), Shade <= 0.9))
    {
        return false;   // no leg can judge the handover's shadow on ground the map leaves lit
    }
    double Means[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };   // [shadows][ground, orbit]
    double P99[2] = { 0.0, 0.0 };                          // [shadows]
    for (int32 On = 0; On < 2; ++On)
    {
        Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
        Test.Ground->SetActorHiddenInGame(false);
        Ship->PlaceShip(Placed.Position, Placed.Orientation);
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        if (!TestTrue(TEXT("at dusk too the ground has the body, the proxy hidden"), Test.Ground->IsDrawingBody() && !Proxy->IsVisible()))
        {
            return false;
        }
        Capture->SetWorldLocationAndRotation(FVector::ZeroVector, Ship->UniverseToWorld(Fourth.Position).GetSafeNormal().Rotation());
        TArray<FLinearColor> Seen;
        Shoot(Seen);
        Means[On][0] = CentralMean(Seen);
        const TArray<FLinearColor> GroundSeen = Seen;
        Test.Ground->SetActorHiddenInGame(true);
        Proxy->SetVisibility(true);
        Shoot(Seen);
        Means[On][1] = CentralMean(Seen);
        P99[On] = GapP99(GroundSeen, Seen);
    }
    Shadows->Set(ShadowsWere, ECVF_SetByCode);
    const double GroundShare = Means[1][0] / FMath::Max(Means[0][0], 1e-12);
    const double OrbitShare = Means[1][1] / FMath::Max(Means[0][1], 1e-12);
    const FString DuskLine = FString::Printf(TEXT("49.9 km over Baemsekai IV at a 3-degree dusk (the map's mean over the view %.3f): ground %.6f (%.6f without the shadow), orbit %.6f (%.6f); the shadow keeps %.4f of the ground's light, %.4f of the orbit's; per-pixel p99 %.2e with it, %.2e without"),
        Shade, Means[1][0], Means[0][0], Means[1][1], Means[0][1], GroundShare, OrbitShare, P99[1], P99[0]);
    AddInfo(DuskLine);
    TestTrue(FString::Printf(TEXT("with the shadow the ground's frame is the orbit's to 1e-3 of it (%s)"), *DuskLine),
        FMath::Abs(Means[1][0] - Means[1][1]) <= 1.0e-3 * Means[1][1]);
    TestTrue(TEXT("the shadow reaches both frames: each is darker with it"), GroundShare < 0.99 && OrbitShare < 0.99);
    TestTrue(TEXT("by the same share, to 1e-3"), FMath::Abs(GroundShare - OrbitShare) <= 1.0e-3);
    TestTrue(FString::Printf(TEXT("and the seam: the shadow grows the per-pixel p99 gap at most 3x (%.2e against %.2e)"), P99[1], P99[0]),
        P99[1] <= 3.0 * FMath::Max(P99[0], 1.0e-4));
```

Then make the report carry both lines: replace `FFileHelper::SaveStringToFile(Line + TEXT("\n"), ...)` with `FFileHelper::SaveStringToFile(Line + TEXT("\n") + DuskLine + TEXT("\n"), ...)`.

- [ ] **Step 4: Run it.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && Tools/eyes.sh Eyes.HandoverParity; cat Saved/Eyes/HandoverParity/report.txt`

Expected: PASS, with two lines. The existing 49.9 km line is unchanged: at the start's sun (62.6 degrees on IV) the day exit leaves the map whole there. On the dusk line both shares are well under 1, within 1e-3 of each other, and the p99 with the shadow is within 3x of without. The shares depend on how much of the central quarter the map shades at 8.8 km texels, 3 degrees up. The leg first finds a placement whose view the map shades by at least a tenth (the line prints the map's mean there). So a share of 0.99 or more now means the shadow did not reach that frame. For the ground, look at `CopyBodyLook`. For both, look at the proxy's `Shadows`, which a flush lands faded in. If no placement holds shade ("the view holds shade" fails), the map is lit along the whole 36 degrees of terminator searched: look at the bake (`.TexelsAreTheProfile`) and the light's frame before the handover.

- [ ] **Step 5: The verdict line, and commit.** Add this to `WorldReliefParityTest.cpp`'s header, after the T2 verdict line, filled in from the run:

`* Cast shadow's map (the developer's ruling on slice (b)'s build, baked, 2026-09-28): PASS -- Baemsekai IV's map at 1,024 columns across the terminator and across the seam, SunShadowMap::Sample vs the GPU <worst> against the float mirror's <worst float> from double, at five footprints to level 2; SUMMARY <the SUMMARY line, verbatim>`

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp && \
git commit -qm "test(eyes): the cast shadow's map held to its C++ mirror on the GPU, and the handover keeps it at dusk

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Prove them.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Surface/SunShadowMap.cpp '    return WorldReliefNoise::LerpF64(Seen[0], Seen[1], C.Blend);' '    return WorldReliefNoise::LerpF64(Seen[0], Seen[0], C.Blend);' Eyes.WorldReliefParity; \
MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp '    To.SetTextureParameterValue(SkyMaterial::ShadowMap, From.K2_GetTextureParameterValue(SkyMaterial::ShadowMap));' '' Eyes.HandoverParity; \
./build.sh
```

Expected: two `KILLED`.
- The mirror that never reads the coarser level misses the GPU's trilinear blend at the footprints between levels.
- The ground that is not handed the map reads the white default: its frame is brighter than the orbit's at dusk, and the shares part.

---

## Task 9: the frame must not rise -- `Eyes.LandingFrame` against the baseline

**Owner:** T (holding `LandingFrameEyesTest.cpp` as Task 1 left it). **Depends on:** Task 8.

**Files:**
- Modify: `Tools/landing_frame_gate.py`, `Tools/test_landing_frame_gate.py`: `--not-rise`
- Modify: `Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp`. The world casts shadows, the bakes are flushed before any case is timed, the header comment is updated, and its verdict line is added.

**Interfaces:**
- Consumes: `Eyes.LandingFrame` and its six cases (Task 1); the baseline in `Saved/Eyes/LandingFrame/baseline`, taken before any shadow existed; `AShipSky::FlushShadowBakesForTest`, `ds.Sky.Shadows` (Task 6); `SkyTestWorld::EShadows` (Task 4).
- Produces: `python3 Tools/landing_frame_gate.py --not-rise <baseline> <after>`. It exits 0 when no case's cost is over its band (the medians' error, floored at `RUN_TO_RUN_MS`), 1 when one is, and 2 when a case is missing. It also produces `Saved/Eyes/LandingFrame/shadows-baked/report.txt`, and the verdict.

**The rule, decided now:** each case's frame with the baked shadow on, minus the same case in Task 1's baseline, may be no more than its band. The band is twice the combined standard error of the two medians, floored at 0.2 ms. With the ten raw times each run takes, it is about 0.6 ms on the measured spread. A case faster than its baseline is GO. The whole frame is reported, not gated. The switch's proof stands as Task 1 built it: at `1.5m_dusk3`, at least 10% of the lit and held pixels taken under half.

- [ ] **Step 1: The tool's failing tests.** Append to `Tools/test_landing_frame_gate.py`, before `if __name__ == "__main__":`:

```python
class NotRiseTest(unittest.TestCase):
    """--not-rise: the baked shadow must cost the frame nothing measurable.
    GO only if no case's cost is over its band -- the medians' error, floored
    at RUN_TO_RUN_MS -- and a case faster than its baseline is GO too."""

    def verdict(self, **after):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), **after)
            return gate.main(os.path.join(root, "base"), os.path.join(root, "after"), 0.0, not_rise=True)

    def test_a_free_bake_is_go(self):
        self.assertEqual(self.verdict(on=10.0), 0)

    def test_a_cost_inside_the_band_is_go(self):
        # The measured spread's band is about 0.6 ms: 0.3 ms cannot be told from nothing.
        self.assertEqual(self.verdict(on=10.3), 0)

    def test_faster_is_go(self):
        self.assertEqual(self.verdict(on=9.0), 0)

    def test_a_rise_over_the_band_is_no_go(self):
        self.assertEqual(self.verdict(on=11.0), 1)

    def test_a_missing_case_is_undecided(self):
        self.assertEqual(self.verdict(on=10.0, skip=("1.5m_dusk3",)), 2)

    def test_the_command_line_takes_the_flag(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "base"), 10.0)
            run(os.path.join(root, "after"), 11.0)
            self.assertEqual(gate.cli(["--not-rise", os.path.join(root, "base"), os.path.join(root, "after")]), 1)
            # Without it the budget is 1 ms, and a cost of 1 ms is too close to call.
            self.assertEqual(gate.cli([os.path.join(root, "base"), os.path.join(root, "after")]), 2)
```

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_landing_frame_gate.py`

Expected: the six new tests ERROR. `main` takes no `not_rise`, and there is no `cli`.

- [ ] **Step 2: The tool.** In `Tools/landing_frame_gate.py`, change `def main(baseline_dir, after_dir, budget=1.0):` to `def main(baseline_dir, after_dir, budget=1.0, not_rise=False):`. Replace

```python
        close = abs(cost - budget) < band
        over = cost > budget and not close
```

with

```python
        if not_rise:
            # Nothing may be added: over is a cost the noise cannot explain.
            close = False
            over = cost > band
        else:
            close = abs(cost - budget) < band
            over = cost > budget and not close
```

Replace the file's last two lines with:

```python
def cli(argv):
    """[--not-rise] <baseline> <after> [budget ms]: --not-rise holds every case to its own band."""
    not_rise = "--not-rise" in argv
    args = [a for a in argv if a != "--not-rise"]
    budget = float(args[2]) if len(args) > 2 else (0.0 if not_rise else 1.0)
    return main(args[0], args[1], budget, not_rise=not_rise)


if __name__ == "__main__":
    sys.exit(cli(sys.argv[1:]))
```

Add to the docstring, after its first paragraph:

```
With --not-rise (the baked cast shadow, whose ruling is that it costs nothing
per frame) there is no budget: GO only if no case's cost is over its band, a
case faster than its baseline included; NO-GO if one is.

    python3 Tools/landing_frame_gate.py --not-rise Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows-baked
```

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_landing_frame_gate.py`

Expected: all tests OK, the old eleven and the new six. The old ones still read `main(..., 1.0)` and are unchanged.

- [ ] **Step 3: The test's world.** In `LandingFrameEyesTest.cpp`, make the world `FSkyWorld Test(TEXT("LandingFrameWorld"), 3000, EShadows::On);`, and after `Test.BeginPlay();` add:

```cpp
    // The cast shadow is baked (the ruling): every world's map before any
    // case is timed, so no bake shares the machine with a timed frame. The
    // tiles' shadows are built by each case's own FlushBuildsForTest.
    Test.Step(1.0f / 60.0f);
    Test.Sky->FlushShadowBakesForTest();
```

In the header comment, replace the paragraph that begins "The cast shadow's gate (the cast-shadow plan, Task 1b and Task 4) is priced here." with:

```cpp
 * The cast shadow is BAKED (the developer's ruling on slice (b)'s build,
 * 2026-09-28): the tiles' vertices carry it, each world's map is baked when
 * the system loads, and the ruling is that it costs nothing per frame. So
 * the gate is that the frame does not rise: each of the six cases -- the two
 * above under the start's sun, and 50 km, 1.5 m and 200 km under the dusk
 * goto's 10 degrees and 1.5 m under 3 -- with ds.Sky.Shadows 1, against the
 * same case in the run with EYES_TAG=baseline, taken before any shadow
 * existed, read by Tools/landing_frame_gate.py --not-rise against the
 * medians' own error. Each case is timed with ds.Sky.Shadows 0 and 1 in ABBA
 * order, five times over after one untimed round; on minus off is reported
 * beside it, and is about 0 by construction.
```

and replace its command lines with:

```cpp
 *   EYES_TAG=baseline      Tools/eyes.sh Eyes.LandingFrame      (before any shadow: Task 1's)
 *   EYES_TAG=shadows-baked Tools/eyes.sh Eyes.LandingFrame
 *   python3 Tools/landing_frame_gate.py --not-rise Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows-baked
```

- [ ] **Step 4: Run it.**

Run (on a quiet machine):

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && \
EYES_TAG=shadows-baked Tools/eyes.sh Eyes.LandingFrame; grep '^case' Saved/Eyes/LandingFrame/shadows-baked/report.txt; \
python3 Tools/landing_frame_gate.py --not-rise Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/shadows-baked
```

Expected: `passed: 1`, six `case` lines each saying `switch present`, and the gate's table ending `GO`. The switch's proof passes at `1.5m_dusk3`, and the coverage there is the tile's vertex shadow's.

On `NO-GO`, the frame has risen, which the ruling says it must not. Look at the cases that rose:
- a proxy case (`200km_dusk10`) is the map's eight loads a pixel;
- a ground case at 50 km (morph near 0) is the map's too;
- a ground case at 1.5 m is the node's branch, since at morph 1 it loads nothing.

Re-run once, quiet, before reading anything: a disturbed run is noise. If it rises again, stop and report the table with the case. The fallback, to be ruled, is the texture's own hardware trilinear sample in place of the eight loads. That is one fetch, but its filter's 8-bit weights would need Task 8's floor re-measured.

- [ ] **Step 5: The verdict line, and commit.** Write into the test's header comment, after the commands:

`Cast shadow, baked (2026-09-28): GO -- not rising; cost over the before-baseline, ms: 50km <a>, 1.5m <b>, 50km_dusk10 <c>, 1.5m_dusk10 <d>, 1.5m_dusk3 <e>, 200km_dusk10 <f>, each within its band (<bands>); whole frames <A>..<F> ms (not gated: the spec's profiling); the switch at 1.5m_dusk3 took <cov>% of the lit and held pixels`

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Tools/landing_frame_gate.py Tools/test_landing_frame_gate.py Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp && \
git commit -qm "test(eyes): the baked cast shadow does not raise the frame -- Eyes.LandingFrame against the baseline, --not-rise

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Prove the switch's assertion, by pixels.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'Instance->SetScalarParameterValue(SkyMaterial::Shadows, ShipSky::ShadowStrength());' 'Instance->SetScalarParameterValue(SkyMaterial::Shadows, 1.0f);' Eyes.LandingFrame; ./build.sh`

(Reviewed 2026-09-28: Task 6 as first built wrote the strength times the fade, and this pattern matched nothing. Since the fade became the map's own `ShadowMapFade` (Task 6's amendment), the line reads exactly this again, once.)

Expected: `KILLED`. With the proxy's strength pinned at 1, and the ground copying the proxy's, the frames with the switch at 0 and 1 are one scene, and the held-pixel coverage falls to the flicker's (0.5-2.3%), under the 10%. The `--not-rise` rule itself is proven by Step 1's tests: a timing mutant against ABBA medians is killed or not by noise alone.

---

## Task 10: the frames after, the numbers, the docs, the suite's time

**Owner:** T. **Depends on:** Task 9 (GO).

**Files:**
- Modify: `Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp`: the world casts shadows, and the bakes are flushed
- Modify: `Tools/relief_look_compare.py`, `Tools/test_relief_look_compare.py`: the brightness on held pixels, before and after
- Modify: `docs/superpowers/specs/2026-09-27-landing-design.md` (on the track): the measured line under the ruling
- Modify: `docs/superpowers/plans/2026-09-27-landing-slice-1.md` (*RULINGS AFTER PLANNING*: one line)
- Modify: `CLAUDE.md`: *Architecture*'s Surface line, *The sky*'s new paragraph, *Landing*'s ground paragraph, and the tunables' rows

**The done-when's frames, decided now:** Baemsekai III, IV and V, from 200 km and at the ground (1.5 m), under the dusk goto's 10 degrees and under 3, each before (Task 1's `shadows-before`) and after (`shadows-after`). All are drawn at the game's exposure plus the view's stated `ReadStops` (the read frames: III/IV/V at 3/3/3 from orbit and 3/4/3 at the ground at 10 degrees; 4/4/4 and 4/5/4 at 3 degrees). Each view reports:
- the mean brightness, on the pixels both runs held still, before, after with the shadow off, and after with it on;
- the shadow's coverage on the pixels the run's two captures without it held still;
- its aliasing against 2x.

- [ ] **Step 1: The comparison's failing tests.** In `Tools/test_relief_look_compare.py`, give `run()` a parameter `on=None` which, when given, also saves `name + "_read_shadows1.png"` from it. Then add to `CompareTest`:

```python
    def test_held_means_are_read_on_the_pixels_both_runs_held(self):
        # The term darkens rows 0-3 to 10; one pixel of row 0 flickered in the
        # run after, so 63 pixels are held in both runs.
        with tempfile.TemporaryDirectory() as root:
            on = frame(40)
            on[:4] = 10
            again = frame(40)
            again[0, 0] = 90
            run(os.path.join(root, "a"))
            run(os.path.join(root, "b"), again=again, on=on)
            before, off, lit = compare.held_means(os.path.join(root, "a"), os.path.join(root, "b"), "world_4_orbit")
            self.assertAlmostEqual(before, 40.0, places=6)
            self.assertAlmostEqual(off, 40.0, places=6)
            self.assertAlmostEqual(lit, (31 * 10 + 32 * 40) / 63.0, places=6)

    def test_held_means_without_the_frame_with_the_term(self):
        with tempfile.TemporaryDirectory() as root:
            run(os.path.join(root, "a"))
            run(os.path.join(root, "b"))
            self.assertIsNone(compare.held_means(os.path.join(root, "a"), os.path.join(root, "b"), "world_4_orbit")[2])
```

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_relief_look_compare.py`

Expected: the two new tests ERROR, because `relief_look_compare` has no `held_means`.

- [ ] **Step 2: The comparison.** In `Tools/relief_look_compare.py`, after `same()`, add:

```python
def held_means(before_dir, after_dir, name):
    """(before, after without the term, after with it): each read frame's mean
    luma over the pixels both runs held still between their own two captures
    without the term -- the done-when's brightness on still pixels. The last
    is None if the run after has no frame with the term."""
    frames = [png(d, name, s) for d in (before_dir, after_dir) for s in ("0", "0b")]
    if any(f is None for f in frames) or len({f.shape for f in frames}) != 1:
        return None, None, None
    was, was_again, now, now_again = frames
    mask = held(was, was_again) & held(now, now_again)
    if not mask.any():
        return None, None, None
    luma = lambda f: float((f[mask] * WEIGHTS).sum(axis=1).mean())
    on = png(after_dir, name, "1")
    return luma(was), luma(now), (luma(on) if on is not None and on.shape == was.shape else None)
```

In `main`, add three columns to the header (`"held before", "held off", "held on"`) and to each row:

```python
        before_mean, off_mean, on_mean = held_means(before_dir, after_dir, name)
        fmt = lambda v: "-" if v is None else "%.2f" % v
```

printing `fmt(before_mean), fmt(off_mean), fmt(on_mean)` after the `coverage` column. Update the docstring's first sentence to name them.

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_relief_look_compare.py`

Expected: all tests OK.

- [ ] **Step 3: The frames test's world.** In `ReliefLookEyesTest.cpp`, make the world `FSkyWorld Test(TEXT("ReliefLookWorld"), 8, EShadows::On);`, and after `Test.BeginPlay();` add:

```cpp
    // The cast shadow is baked: every world's map before any frame.
    Test.Step(1.0f / 60.0f);
    Test.Sky->FlushShadowBakesForTest();
```

In its header comment, replace "Planning measured (the cast-shadow plan) that physical relief casts on under 3% of the ground at ten degrees and on 40-60% at three" with "Planning measured (the cast-shadow plan) that physical relief casts on under 3% of the ground at ten degrees and on 40-60% at three at a pixel's footprint; the bake's are the tiles' vertices on the ground and the map's 8.8 km texels from orbit, where it measured 15-19% on IV and V at three".

- [ ] **Step 4: The frames after.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && EYES_TAG=shadows-after Tools/eyes.sh Eyes.ReliefLook && python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after`

Expected: a table of 12 frames, exit 0. Every shadows-off read frame is the frame before on the pixels both runs held: at most 10% moved by more than 1 LSB, and the rest within a quarter level of mean luma. So `ds.Sky.Shadows 0` is the old look, and an unequal CRC is reported, not failed. A frame marked `ALIASING?` is an edge switching inside a pixel; report it with the frame. Planning's predictions (a frame far from its prediction is a finding to report, not a failure):
- Orbit, 10 degrees: coverage about 0% on III, IV and V (the map's 8.8 km texels, and a sun that clears nearly every horizon).
- Orbit, 3 degrees: about 0% on III, and about 15-19% on IV and V.
- Ground, 10 degrees: about 2-5% on IV and 5-8% on V.
- Ground, 3 degrees: about 40% on IV and 50% on V.

The held means fall with the coverage: after-on is under after-off by about the shaded share.

If the comparison prints `OFF FRAME DIFFERS`, the switch is not the old look beyond compiler noise, so find out why and do not go on. The likeliest cause is a material that does not multiply by exactly 1 at strength 0: check `cast()`'s lerp.

- [ ] **Step 5: Look at the frames.** Open `Saved/Eyes/ReliefLook/shadows-after/world_4_ground_low_read_shadows1.png` and its `read_shadows0` twin, then the same pair for `orbit_low`, then both 10-degree pairs. Check that:
- the shadows fall away from the sun, and lie across the view in the ground frames;
- no speckle, banding or tile seam shows where the unshadowed frame has none;
- no shadow steps at an edge between two tiles of one level. Their shared vertices are the same directions at the same footprint, so the shadows are the same numbers, which `.TileSunShadow` holds to the bit;
- **at a 2:1 edge, look for a step in the shade.** Only tiles of the same level agree at a shared edge. Where a level-L tile meets a level-L+1 one, a shared direction is shadowed at two footprints, and the finer side's odd vertices carry values the coarser side interpolates. The skirts close the crack in the geometry but not the step in the shade. `.TileSunShadow` printed the step's size under a 2-degree sun (Task 4's report). If it shows in the 3-degree ground frames, record where and how large. Put it to the developer with the remedy: shadow each tile's edge ring at twice its spacing, the coarser side's footprint, and make its odd edge vertices the mean of their two neighbours. That holds a 2:1 edge only one way, so it is a ruling. Do not tune it away here;
- the terminator side is darker and never brighter;
- from orbit the map's shadows are soft at its texel, and never blocky: a blocky edge is the lookup's level or its bilinear taps.

Write a one-line judgement per world into the spec line below. The developer's own look is carried to the next playtest.

- [ ] **Step 6: The spec.** In `docs/superpowers/specs/2026-09-27-landing-design.md`, under the ruling "**Cast shadows are baked, not marched** (2026-09-28)", add an indented line with the runs' numbers:

```markdown
  - **Measured, 2026-09-28** (plan `2026-09-28-landing-b-cast-shadows.md`): baked. Each tile's vertices march `SunShadow::Visible` (<N> samples) at the tile's spacing under the sky's own light, craters and every band of `Height` in; each solid world's map is <W> columns (<texel> km texels on IV), baked when the system loads. The bake: the cold cut at 1.5 m <cut> s (was <cut0>), a tile <ratio>x its heights at a 10-degree dusk, a world's map <world> s and <MB> MB (the slowest and largest of I-V and the corpus's two extremes), the sky's five <sky> s on <tasks> tasks, <gpu> MB resident on the GPU and <cpu> MB left on the CPU (the corpus's worst system about <worst> MB), the slowest landing <land> ms; an in-system jump re-bakes <rebakes>; a release with builds in flight <release> ms; in flight with tile shadows on, <missing> frames without drawn ground, worst gap <gap> cm, the coarse cut <coarse> s after arriving; a 2:1 tile edge steps the shade by <step> on average. The frame: not rising (`Eyes.LandingFrame --not-rise`, every case within its band; whole frames <A>..<F> ms, handed to the ground's profiling). Parity: the map C++ vs GPU <worst> against the float mirror's <float>; the handover at a 3-degree dusk ground <g> vs orbit <o> (<share> of the light kept). Frames at the read exposure, mean luma on held pixels before -> after (coverage): at 10 degrees, orbit III <x> -> <y> (<c>%), IV ..., V ...; ground III ..., IV ..., V ...; at 3 degrees, orbit ..., ground .... Judged: <one line a world>. The developer's look carried to the playtest.
```

- [ ] **Step 7: The parent plan and CLAUDE.md.** (Reviewed 2026-09-28: the Architecture line, the *Landing* insert, the three table rows and the named constants below are already in CLAUDE.md; what is left is the *Sky* paragraph and the parent plan.) In `docs/superpowers/plans/2026-09-27-landing-slice-1.md`, append to the *RULINGS AFTER PLANNING* paragraph: "**Cast shadows (ruled 2026-09-28: baked, not marched)** are their own plan, `2026-09-28-landing-b-cast-shadows.md`, owned by track T."

In `CLAUDE.md`, *Architecture*'s `Source/DeepSpace/Surface/` bullet, after "`AWorldGround`, which streams ... (*The ground*).", add: "`SunShadow.*` (the cast shadow's pure horizon march) and `SunShadowMap.*` (each world's baked map for the orbit) are the shadow (*The sky*)."

In *The sky*, after the paragraph that begins "**A world's face and relief are one noise.**", add:

```markdown
**The ground casts shadows, baked, never marched per pixel** (the developer's
ruling on slice (b)'s build: a per-pixel march cost +4 to +27 ms). Worlds do
not spin, so the star stands still over every surface and the shadow is a
fixed function of where a point is. `SunShadow` (`Surface/SunShadow.*`, pure)
finds the highest horizon toward the star along the great circle, by the
exact triangle, over `FWorldRelief::Height` -- every band and the craters --
and shows the share of the star's own disc above it
(`SkyProjection::StarAngularRadius`, clamped to 0.5 rad); a shadow is as dark
as the night side. Its light is the sky's own, `SkyProjection::LightDirection`.
Each ground tile computes its vertices' shadow as it is built, off the game
thread (`TerrainTile::Build`, 12 samples, `ds.Terrain.ShadowSamples`), into
UV0.x, so a vertex shadow is as sharp as its tile's spacing: 12-24 pixels
between vertices at 4K on the drawn cut. Two tiles of one level agree on
their shared edge to the bit; at a 2:1 edge the shade can step, since the
skirts close only the geometry's crack (`.TileSunShadow` measures it). Each solid world's map for the orbit
(`SunShadowMap`: equirectangular in the star's frame, 4096 columns,
`ds.Sky.ShadowMapWidth`, 8.8 km texels on Baemsekai IV, the provably dark
night side left out, 16 bits and a mip a level) is baked by `AShipSky`,
nearest first, `ds.Sky.ShadowBakeTasks` at once at low priority, and **keyed
by what it is made from** -- the relief, the sky's light for the world, the
width -- never by the jump serial: an in-system jump bumps the serial and
rebuilds the proxies, and re-bakes nothing, while `ds.Universe.ReloadPriors`
re-bakes exactly the worlds whose relief moved. A dropped bake is cancelled
and let go, never waited on. Before a map lands the world draws unshadowed,
and when it lands its shadow fades in over `ShipSky::ShadowFadeSeconds`
(1 s). The texture is in `TEXTUREGROUP_Pixels2D`, so no device profile's
LOD bias drops mip 0 under the lookup, and its CPU copy is discarded once
uploaded. It is finer than a 4K pixel only while the world is under about
35 degrees across -- the lookup is handed one pixel's footprint, the faces'
over `filter_pixels` -- and softer nearer: no per-world texture can be finer
than the screen from 50 km. Under its lowest row, `PsiLo`, it reads 0. Both
materials read the map through one Custom node over the shared file's
`WR_ShadowMapCoord` (`SunShadowMap::Sample` mirrors it, and
`Eyes.WorldReliefParity` holds them together), and M_SkyGround blends it into
its vertices' shadow by the same `Morph` that grows the relief in, so at the
50 km handover the ground's shadow is the orbit's (`Eyes.HandoverParity`, at
dusk). The night's exit is a proof, the day's a sampled steepest slope with a
1.5 margin (`SunShadow::SteepestSlope`): a slope steeper than any sample
would show as a lit speck under a high sun. `ds.Sky.Shadows 0` draws the
unshadowed look. It costs the frame nothing (`Eyes.LandingFrame`, `--not-rise`
against the frame before any shadow); what it costs the bake is
`Eyes.ShadowBakeCost`'s, which also flies `GroundKeepsUp`'s flight with the
tile shadows on. Test worlds build without either unless asked, and ask for
each apart (`SkyTestWorld::EShadows::Tiles`, `::Maps`, `::On`); every test
of the ground in flight asks for `::On`, as the game ships, and
`GroundKeepsUpAtDusk` flies the 10-degree dusk where the tiles' shadow
costs most. The map's fade-in is its own (`ShadowMapFade`): the ground's
vertex shadow draws before any map lands, and with `ds.Sky.ShadowMaps 0`.
```

In *Landing*'s paragraph that begins "**The ground** (`AWorldGround`, `hauler_ground`)", after "shaded by `M_SkyGround` exactly as the orbit shades", add: "(each tile's vertices carrying the cast shadow, *The sky*)".

In *Where each tunable lives*, after the `ds.Sky.SurfaceDetail` row, add:

```markdown
| `ds.Sky.Shadows` | 1 (0 draws the unshadowed look) | `ShipSky.cpp` |
| `ds.Sky.ShadowMaps`, `.ShadowMapWidth`, `.ShadowBakeTasks` | 1, 4096 columns (a power of two, 256-8192), 2 at low priority; a change to the width re-bakes every map | `ShipSky.cpp`, the width from `SunShadowMap::DefaultWidth` (`SunShadowMap.h`) |
| `ds.Terrain.Shadows`, `.ShadowSamples` | 1, 12; changing either rebuilds the ground | `WorldGround.cpp`, the samples from `SunShadow::DefaultSamples` (`SunShadow.h`) |
```

Add to the named-constants sentence at the table's foot: "the cast shadow's `SunShadow::SteepestMargin` (1.5) and sampled gradients (`DetailGradientSampled`, `CraterGradientSampled`, re-measured by `DeepSpace.Surface.SunShadow.SteepestSlope`), and its maps' fade-in, `ShipSky::ShadowFadeSeconds` (1 s)".

- [ ] **Step 8: The whole suite, and its time.**

Run:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./test.sh DeepSpace && \
python3 - <<'PY'
import datetime, re
lines = open("Saved/Logs/DeepSpace.log", errors="replace").read().splitlines()
stamp = lambda line: datetime.datetime.strptime(line[1:24], "%Y.%m.%d-%H.%M.%S:%f")
new = ("DeepSpace.Surface.SunShadow.", "DeepSpace.Surface.SunShadowMap.", "DeepSpace.Surface.TileSunShadow",
       "DeepSpace.Surface.GroundShadowLight", "DeepSpace.Sky.SunLightIsTheSkys", "DeepSpace.Sky.ShadowParameters")
started, total = {}, 0.0
for line in lines:
    m = re.search(r"Test Started\. Name=\{[^}]*\} Path=\{([^}]*)\}", line)
    if m:
        started[m.group(1)] = stamp(line)
    m = re.search(r"Test Completed\. Result=\{[^}]*\} Name=\{[^}]*\} Path=\{([^}]*)\}", line)
    if m and m.group(1).startswith(new) and m.group(1) in started:
        seconds = (stamp(line) - started[m.group(1)]).total_seconds()
        total += seconds
        print("%-52s %6.2f s" % (m.group(1), seconds))
print("this plan's additions to the default suite: %.2f s" % total)
PY
```

Expected: all green, with no dirty-log gate tripped. The additions total at most 8 s, with no test over 5 s. If the pattern finds nothing, read one `Test Started` and one `Test Completed` line in the log and fix the pattern, never the budget. Over 8 s, stop and report the per-test times, so the developer chooses what moves out of the default suite. Then run the rendered and timed checks once more, together:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && Tools/eyes.sh Eyes.WorldReliefParity && Tools/eyes.sh Eyes.HandoverParity && \
EYES_TAG=final Tools/eyes.sh Eyes.LandingFrame && python3 Tools/landing_frame_gate.py --not-rise Saved/Eyes/LandingFrame/baseline Saved/Eyes/LandingFrame/final && \
(cd Tools && python3 test_relief_look_compare.py && python3 test_landing_frame_gate.py)
```

Expected: all green, and the gate GO.

- [ ] **Step 9: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp Tools/relief_look_compare.py Tools/test_relief_look_compare.py \
  CLAUDE.md docs/superpowers/specs/2026-09-27-landing-design.md docs/superpowers/plans/2026-09-27-landing-slice-1.md && \
git commit -qm "docs: the ground casts shadows, baked -- measured, framed, and in CLAUDE.md

Eyes.ReliefLook's frames after (III, IV and V from 200 km and at the ground,
at 10 and 3 degrees, the brightness on held pixels before and after); the
spec's measured line; the tunables.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 10: Prove the comparison's new measure.** `Tools/mutate.sh` builds and runs Unreal tests, so a Python mutant is proven by hand, as Task 1's tools were:
  - In `held_means`, change `mask = held(was, was_again) & held(now, now_again)` to `mask = held(was, was_again)`.
  - Run `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_relief_look_compare.py`, and see `test_held_means_are_read_on_the_pixels_both_runs_held` fail: 64 pixels, not 63.
  - Restore it with `git checkout -- Tools/relief_look_compare.py`, and record the check in the task report.

**Done when:**
- Task 0's three questions are answered and written into the spec.
- `DeepSpace.Surface.SunShadow.*` holds the pure march to its known values, exits, schedule and quality against the dense march, and its sampled slope to the real ground's.
- `DeepSpace.Surface.TileSunShadow` and `.GroundShadowLight` hold every tile's vertices to that march under the sky's own light. `DeepSpace.Sky.SunLightIsTheSkys` holds that light to the sky's.
- `DeepSpace.Surface.SunShadowMap.*` holds each world's map to its profile, its dark rows to the proof, and its lookup to its known values, 0 under `PsiLo` included.
- `DeepSpace.Sky.ShadowParameters` holds the sky's bakes and the materials' parameters: keyed maps, a jump within the system re-baking nothing, a reload re-baking only what moved, the fade-in, the unbiased LOD group, and no CPU copy.
- `DeepSpace.Sky.MaterialContract` holds the faces to handing the lookup one pixel's footprint.
- `Eyes.WorldReliefParity` holds the map's lookup on the GPU to its C++ mirror at the measured floor, across the terminator and across the seam.
- `Eyes.HandoverParity` holds the ground to the orbit at a 3-degree dusk with the shadow on.
- `Eyes.ShadowBakeCost`'s every budget reads `within`, or Task 7b's ruling is recorded. That covers the flight with tile shadows on, the release, the jump, the landing hitch and the system's memory as the engine counts it.
- `Eyes.LandingFrame` does not rise in any case (`--not-rise`), and its switch is proven by pixels.
- The before/after frames of Baemsekai III, IV and V are in `Saved/Eyes/ReliefLook/shadows-{before,after}`: from 200 km and at the ground, at 10 degrees and at 3, at the game's exposure plus the stated read stops. Each has its mean brightness and shadow coverage on still pixels and its aliasing, recorded in the spec. Every shadows-off read frame is its before on the pixels both runs held.
- The default suite's additions are at most 8 s.
- The orchestrator merges `feat/landing-b-t` into `feat/landing-b`.

---

## Self-review

- **The orchestrator's six points, each owned:**
  1. The pure shadow function over FWorldRelief, the horizon march toward a fixed sun direction with the sun's angular size for softness, with known-value tests: Task 2 (`SunShadow`, over `IGroundField`, whose real field is `FReliefGround` over `FWorldRelief`), with `.KnownValues`, `.Exits`, `.Schedule`, `.AgainstProfile` and `.SteepestSlope`.
  2. Tiles compute a per-vertex shadow during their off-game-thread build, carried in a vertex channel, shading in M_SkyGround: Task 4 (`TerrainTile::Build`, `UV0Of`, `AWorldGround`'s tasks) and Task 6 (M_SkyGround's `lerp(map, UV0.x, Morph)`).
  3. The orbital proxy reads a per-world map at a stated resolution, chosen against the pixel and measured, baked in C++ at system load off the game thread, used by M_SkyBody: Task 5 (`SunShadowMap`, equirectangular, 4096 columns stated with its texel-against-pixel table in *Measured while planning*, the impossibility at 50 km put to the developer in Task 0) and Task 6 (`AShipSky`'s bakes, M_SkyBody). The texel against the pixel is measured again in Task 10's frames, and ruled in Task 0.
  4. Both agree at the 50 km handover (a parity check under the float-floor rule) and use the sky's sun direction:
     - The handover is Task 6's Morph blend (the ground's shadow *is* the map at Morph 0) and Task 8's `Eyes.HandoverParity` dusk leg.
     - The lookup is held on the GPU by Task 8's `Eyes.WorldReliefParity` leg under the float-floor rule.
     - The sun direction is one function: Task 3's `SunLightIsTheSkys`, Task 4's `GroundShadowLight`, and Task 6's `ShadowParameters` (`FrameZ` against `SunLightOf`).
  5. The bake's cost per tile, per world and per system, and memory, measured and budgeted; LandingFrame must not rise: Task 7 (`Eyes.ShadowBakeCost`, fourteen budgets, Task 7b on over) and Task 9 (`--not-rise`).
     - The flight with tile shadows on is `GroundKeepsUp`'s own, shared through `GroundKeepsUpScenario.h`.
     - Memory is read from the engine and from the arrays, not from formulas.
  6. The done-when's frames: Task 10. III, IV and V, from 200 km and at the ground, at 10 and 3 degrees, at the game's exposure plus the stated read stops, with mean brightness and coverage on still pixels.
- **What was kept:** Tasks 1 and 1b's code and verdicts, the gate tool (extended, not changed, by `--not-rise`), `Eyes.ReliefLook`'s still-pixel rule (unchanged; the held means added beside it), `Eyes.LandingFrame`'s ABBA rounds and switch proof (unchanged), and the exposure fix (`EyesFrames::Expose`, used as it is).
- **Names across tasks:**
  - `SunShadow::FSunLight` / `FSunVisibility` / `Visible` / `AlongProfile` / `SteepestSlope` are Task 2's, used unchanged in Tasks 3-8.
  - `TerrainTile::FTileShadow` is Task 4's, read by Task 7.
  - `FSunShadowMap` and `SunShadowMap::*` are Task 5's, used by Tasks 6-8.
  - `SkyMaterial::{Shadows, ShadowMap, ShadowFrameX, ShadowFrameZ}` are Task 6's, used in Task 8.
  - `ds.Sky.Shadows` is used by name in Tasks 1, 6, 8 and 9.
  - `ds.Terrain.Shadows` and `ds.Sky.ShadowMaps` are set by name in `FSkyWorld`, from Task 4 on.
  - The shared file's `WR_ShadowMapCoord`'s 13 arguments are the same in the file, in `ShadowMapCoordF64/F32` and in `SHADOW_CODE`. The node's pins are `SkyMaterial::ShadowInputs()` and the JSON's `shadow.inputs`, held by `CheckShadowNode` and by `sun_shadow`'s own check.
- **No test path is a parent of another.** The new leaves are:
  - `DeepSpace.Surface.SunShadow.{KnownValues, Exits, Schedule, AgainstProfile, SteepestSlope}` and `DeepSpace.Surface.SunShadowMap.{Shape, TexelsAreTheProfile, NightBelowTheMap, Lookup, Mips}`;
  - `DeepSpace.Surface.TileSunShadow` and `DeepSpace.Surface.GroundShadowLight`, siblings of `DeepSpace.Surface.Tile` and `.GroundActor`, not children;
  - `DeepSpace.Sky.SunLightIsTheSkys` and `DeepSpace.Sky.ShadowParameters`;
  - `Eyes.ShadowBakeCost`.

  Every headless mutation filter is anchored with `$`.
- **Honest limits, stated where they bite:**
  - The map cannot be finer than the screen from 50 km (Task 0, question 1).
  - A vertex shadow is as sharp as its tile's spacing (CLAUDE.md's paragraph).
  - The day exit is a sampled claim (Task 0; `.SteepestSlope`).
  - A world draws unshadowed until its map lands (Task 7b's `world_bake_s`), then fades in over a second.
  - The shade can step at a 2:1 tile edge (Task 4 measures it, Task 10 looks for it).
  - The CPU copy's discard rests on `RemoveBulkData` being allowed on a transient mip. Task 6 checks it, and Task 7 measures it.
- **Review fixes, where each is pinned:**
  - the maps keyed per world (`.ShadowParameters`, Task 6);
  - the flight with tile shadows on (`Eyes.ShadowBakeCost`, Task 7);
  - no waits in teardown (`GroundShadowLight`'s cap, Task 4; `release_ms`, Task 7);
  - the 2:1 edge (`.TileSunShadow`, Task 4; Task 10 Step 5);
  - a pixel's footprint (`.MaterialContract`, Task 6);
  - the per-system memory (Task 0; `system_gpu_mb`, Task 7);
  - 0 under `PsiLo` (`.Lookup`, Task 5);
  - the dusk handover's shaded view (Task 8);
  - the fade (`.ShadowParameters`);
  - the split switches (`EShadows`, Task 4);
  - the LOD group and the landing's hitch (Tasks 6, 7);
  - the worst worlds (`world_bake_s`, `world_map_mb`, Task 7).
