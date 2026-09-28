# Landing Slice (b): Cast Shadows Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put cast shadows on the ground. A march toward the sun through the one height function, written once in `Shaders/Private/WorldRelief.ush`, is used by `M_SkyBody` and `M_SkyGround` alike. A C++ mirror holds it by `Eyes.WorldReliefParity`, its GPU cost is measured in `Eyes.LandingFrame`, and before/after frames judge it.

**Architecture:** `WR_SunVisible` is added to the shared file in its scalar, no-swizzle subset. It marches a fixed number of geometric samples toward the sun's azimuth, reading the GPU's twelve detail bands. Each sample reads at twice its own spacing, which is the face's own fade. It returns the share of the star's disc above the highest horizon it found. C++ compiles it in double as the mirror (`WorldReliefShading::SunVisible`) and in float as the floor's measure. Both materials call it through one more Custom node, with the same D, footprint and light (in body axes on the proxy, in tile-local axes on the ground). So the 50 km handover keeps it. `ds.Sky.Shadows` switches it, and the switch also skips the march, which is how the budget is timed.

**Tech Stack:** Unreal Engine 5.8.2 C++ (module `DeepSpace`); HLSL in material Custom nodes through the engine's `/Project` mapping; Python editor scripting (`Tools/setup_sky_materials.py`); automation tests through `./test.sh`, rendered checks through `Tools/eyes.sh`, mutation proofs through `Tools/mutate.sh`.

**Spec:** `/home/matt/Development/deepspace/docs/superpowers/specs/2026-09-27-landing-design.md`. The ruling is *Ruled on slice (b)'s build, 2026-09-28*, first bullet ("Cast shadows"). The parent plan is `docs/superpowers/plans/2026-09-27-landing-slice-1.md` as it stands on `feat/landing-b`. Its *Conventions for every task* bind here. Its *RULINGS AFTER PLANNING* set the parity rule that this plan applies to a new term.

## Global Constraints

- The ruling, verbatim: "the shared file gains a **shadow term marched through the height function toward the sun**, used by `M_SkyBody` and `M_SkyGround` alike. It is held to a C++ mirror by the parity test, its GPU cost is measured in `Eyes.LandingFrame`, and it is judged by before/after frames. Sign-off 2 stands as physical heights; it is not reversed to an exaggerated orbit."
- The height the march reads is `FWorldRelief::Height`'s, `PeakCm x PeakCap(S / S_max)`. `S_max` is `FWorldRelief::SMaxMeasured` (0.029932993600784347) and the cap's knee is 0.8. Heights are physical and are never exaggerated.
- The shared file stays in the scalar, no-swizzle subset. Every symbol is `WR_`-prefixed, and every literal is `WR_REAL(...)`. A syntax either compiler refuses fails that compiler: `./build.sh` for C++, `Eyes.WorldReliefParity` for the GPU.
- Parity follows the measured floor as a rule, per footprint and per term. The tolerance is 1e-3, or 1.25 x the float floor's distance from double at that footprint, whichever is larger. The new term has no engine nodes, so its float floor is the shared file's own float build, measured in the same run at the same D. A term over its allowance is a port bug. A FLOAT FLOOR verdict escalates, and the tolerance is never loosened.
- `Eyes.LandingFrame` at 4K (3840 x 2160) on the RTX 4070 Ti SUPER stays at or under 16.6 ms in every case. The cast shadow's own added cost is at most **1.0 ms** in every case, which is the terrain budget's spare: `Eyes.TerrainBudget` measured 4.96 ms of its 6 ms.
- Where it applies: the orbit proxy and the ground tiles, at every altitude, with the same function, D, footprint and light. The handover at 50 km stays seamless, which `Eyes.HandoverParity` holds to 1e-3 of the mean.
- Black sky: no fill light, so a shadow is as dark as the night side. Worlds do not spin, and giants and oceans cast nothing.
- Anti-chore: nothing here is a timer, a charge or a state.
- All logic is C++ or the shared file. The material graph only wires. The material contract has three sides: `SkyMaterialContract.h`, `Tools/sky_material_contract.json` and the assets.
- Build only with `./build.sh`, test only with `./test.sh`, render only with `Tools/eyes.sh`, and mutate only with `Tools/mutate.sh`, all behind `Tools/ue_lock.sh`. The editor is closed. At most 3-4 workers, `nice -n 19` for any local sweep.
- Every test path is a sibling with no children. Every new test is proven able to fail.

## Review Focus

These are the five inputs the ruling implies and no happy-path test meets, most likely first. Each is pinned in its owning task.

1. **The sun at the zenith, L = D.** The sun's azimuth along the ground is undefined, and a normalised zero is NaN. The expected result is a whole disc, finite. Pinned by `DeepSpace.Surface.WorldRelief.SunDisc` in **Task 2**.
2. **A world with 1 cm of relief.** The march's end falls inside its first sample, and the only horizon is the sphere's own. The expected result is 0.5 with the sun's centre on the level, 1 above by two radii and 0 below, monotone between, all finite. Pinned by `.SunDisc` in **Task 2**.
3. **A giant or an ocean.** A giant's `ReliefScale` is its cloud billow (0.066), not ground, and an ocean's is 0. Neither may cast a shadow. The C++ half is pinned by `.SunDisc`'s no-ground case in **Task 2**. The material half is pinned by `DeepSpace.Sky.MaterialContract`'s check that `M_SkyBody`'s shadow reads `ReliefScale x (1 - Banding)`, in **Task 3**.
4. **The night side and the edges of the exits.** Past the steepest slope the ground can have, either way, the march is skipped. The skip must meet the march with no step, or the terminator shows a seam. Pinned by `.SunDisc`'s four cases at `+-(Steepest + r)` and `+-(Steepest + r/2)` in **Task 2**.
5. **`ds.Sky.Shadows` out of range at the console** (7, -1, 0.5). The value is clamped to 0..1. 0 must skip the march and draw exactly the unshadowed look, because the budget's A/B and the before frames depend on it. A star seen from inside its own radius gives an arcsine argument over 1. Pinned by `DeepSpace.Sky.ShadowParameters` in **Task 3**, and by Task 6's pixel-identity check of the shadows-off frames.

---

## Measured while planning

The design points the task set were settled by measurement before this plan was written. The measurements used a standalone C++ build of the shared file (`WorldRelief.ush` at `feat/landing-b` `1b8234c`, compiled with `WR_CPP` in double and in float) together with the march's own text from Task 2, outside Unreal. The HLSL half of the same text was compiled by `glslangValidator -D -V`.

"Random ground" here means random seed offsets (multiples of 1/256), random directions, and the sun at a fixed elevation with a random azimuth. Each world used its real radius and relief (`Saved/procgen_corpus.tsv`) and its real star size: Baemsekai is 0.16 R_sun, so its angular radius is 2.95 deg from III, 1.66 deg from IV and 0.97 deg from V. **Truth** is the same band-limited height at the pixel's footprint, marched densely (one step per eighth of the finest kept band's wavelength) under the same disc. **Shadowed** means visibility under 0.5.

### Finding 1: the ruled dusk casts little shadow

At the dusk goto's 10 degrees, physical relief casts almost none.

| Share of ground shadowed (truth) | 10 deg | 5 deg | 3 deg | 2 deg |
|---|---|---|---|---|
| III from 200 km (pixel 4e-5 R) | 0.0% | 0.0% | 0.3% | 6.3% |
| IV from 200 km | 0.3% | 19.3% | 43.3% | 56.0% |
| V from 200 km | 1.7% | 28.3% | 47.7% | 62.0% |
| IV at the ground (pixel 1e-7 R) | 2.0% | 30.7% | 46.7% | - |
| V at the ground | 5.3% | 42.0% | 59.3% | - |

The steepest slope in the GPU's twelve bands is 5.5 deg on III (20,000 samples), 20.8 deg on IV and 23.1 deg on V. The medians are 1.6, 5.7 and 6.7 deg. A sun 10 degrees up therefore clears nearly every horizon, and on III it clears every one.

`Eyes.ReliefLook`'s 200 km frames look straight down the nose. Their 60-degree field spans only about +-1.2 degrees of sun elevation, so the whole ruled frame sits at 10 degrees, and **the shadow term will barely change the ruled before/after frames.** They are still built and reported, because they are the ruled done-when. Beside them, the frames test also takes the same views at **3 degrees**, where the term is plainly visible. That is the only addition to what was asked.

Craters are not what the term restores. A crater wall is at most `2 x crater_depth x ReliefScale` steep, which is 1.5 degrees on IV, so it casts nothing under any sun above that. That finding matters to the developer's judgement and is repeated in Task 6's report.

### Finding 2: the march

| Design point | Settled | Measured |
|---|---|---|
| Which bands | The GPU's twelve detail bands (24..49,152 cycles per radius), each faded by the footprint rule. No crater bands. | With craters marched (N 16), shadowed at 5 deg IV rose 11.0% -> 11.8% and at the ground 15.3% -> 17.3%, for six more bands of 27 cells of hashes per sample, about 60% more work. |
| Sample count | 12 (`shadow_samples`). Task 4's budget gate may lower it to 8 or 6. | Ratio to truth at 5 deg, IV / V: N 12 0.84 / 0.79; N 8 0.81 / 0.78; N 6 0.79 / 0.79. At 2 deg: N 12 0.82 / 0.91; N 8 0.82 / 0.86; N 6 0.77 / 0.84. N 24 gains about 2 points over N 12. |
| Step growth | Geometric from `Nearest = max(pixel footprint, half the finest band's wavelength)` to `End`. `End` is where the sun's lower edge, rising at `tan(theta - r)` with the ground curving away as `s^2 / 2`, clears `PeakCm` from the pixel's own height. | `End` is about 0.027 R at 5 deg on IV (150 km) and 0.06 R at 2 deg. |
| Footprint rule | Each sample reads its bands at **twice its own spacing** (`WR_SHADOW_FOOTPRINT_FACTOR` 2, the face's `filter_pixels`). A band has faded to nothing by the time it has two samples per cycle, so the march never aliases. | The rule costs up to a sixth of the truth's shadow compared with no fade, all at 5 deg: 22.3% against no fade's 25.3% on V from orbit (truth 28.3%); at the ground 24.0% against 27.3% on IV (30.7%) and 30.0% against 35.3% on V (42.0%). From orbit on IV the two are equal, at 16.3%. The fade is the price of never reading a band under two samples a cycle. |
| Height compared against | The pixel's own full height at its footprint. | A variant that compared each sample against the pixel's height at the sample's own footprint was worse in every case (mean \|dv\| 0.061 vs 0.064 at 5 deg IV, 0.100 vs 0.119 at 2 deg V), so it was dropped. |
| Softness | The share of a disc of the star's angular radius `r` above a straight horizon: `(acos x - x sqrt(1 - x^2)) / pi`, with `x = (H - theta) / r`. `r = asin(R_star / d)` per body (`ShipSky::SunAngularRadius`). | The penumbra covers 11% of the probe patch on IV and 7% on V. |
| Exits | Above `theta - r >= atan(ReliefScale x 21.3)` the disc is whole. Below `theta + r <= -that` it is hidden. Inside the loop there are two exact exits: the disc is already hidden, or no ground from here on can rise above the horizon found. | The steepest `\|grad S\|` over 262,144 samples is 14.19, so 21.3 is 1.5 x that. This gives 34 deg on IV and 39 deg on V: the day side under a high sun costs nothing. |
| Parity floor | The float build against double, measured per footprint in the same run. | Max visibility gap on IV / V: at 1/96 1.7e-5 / 4.3e-5, at 1/768 2.1e-4 / 4.1e-4, at 1/3072 1.1e-3 / 2.1e-3, at 1/12288 5.3e-3 / 1.2e-2. Mean gaps are under 3e-5. At 60 deg the gap is exactly 0 (the exit). |

### Finding 3: the budget is the risk

A value-only simplex band in the file is about 260 GPU instructions: four corners, each an LCG and six Feistel rounds, a gradient and a smoothstep. A marching pixel reads, per sample count:

| Per marching pixel | 10 deg | 5 deg | 2 deg | Ground, 10 deg |
|---|---|---|---|---|
| N 12 | 98 bands | 86 | 66 | 107 |
| N 8 | about 66 | about 60 | about 53 | about 72 |
| N 6 | 50 | 45 | 37 | 55 |

The face's own terms per pixel are 12 gradient bands, 6 crater bands of 27 cells and the continent, about 16,000 instructions. **N 12 is therefore about 1.5 times the face's own shading cost, N 8 about 1.0 and N 6 about 0.75**, wherever the sun is below the exit. At dusk that is every pixel of the ground.

At 4K on a 4070 Ti SUPER this estimate says a full-screen march exceeds the 1 ms budget by several times. The estimate is a count, not a measurement. So the budget gate (Task 4) comes straight after the materials and before parity and frames. If N 6 does not fit, it stops the plan at a re-plan point (Task 4b), and a NO-GO costs one task.

The fallback it would put to the developer is a genuinely different design. Worlds neither spin nor move, so the sun is fixed in each world's axes, and the shadow is a fixed function of D per world. It can be baked by this same `WR_SunVisible`, drawn with `DrawMaterialToRenderTarget` (the parity probe's own mechanism) into a clipmap centred on the ship, and sampled by both materials.

---

## Execution: tree, order, ownership

Track T owns this work (parent plan: `setup_sky_materials.py`, the contract, `Surface/WorldRelief.*`, `ShipSky.*`). It runs in `/home/matt/Development/deepspace/.worktrees/landing-b-t` on `feat/landing-b-t`. For this work, T also takes `Eyes.LandingFrame`, which the orchestrator wrote in Task 39 (Z), and a paragraph of CLAUDE.md. The orchestrator merges `feat/landing-b-t` into `feat/landing-b` when Task 6 lands.

```
Task 1  the frames before (Eyes.ReliefLook, ground and 3-degree views, stats)   -- run BEFORE any shader change
Task 2  WR_SunVisible in the shared file, pure, and its C++ mirror; headless tests
Task 3  the materials, the contract, the sky's parameters (SunRadius, Shadows)
Task 4  THE GATE: Eyes.LandingFrame A/B at 4K; the sample count; GO or NO-GO
Task 4b (only on NO-GO) a stop-and-replan point: the report goes to the developer
Task 5  parity: Eyes.WorldReliefParity's shadow leg; Eyes.HandoverParity at dusk
Task 6  the frames after, the numbers, the docs
```

**First, in the tree:** it lacks `Eyes.LandingFrame` and the *Landing* section of CLAUDE.md that `feat/landing-b` has (`1b8234c`). Run this before Task 1:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git status --short && git merge -q feat/landing-b && git log --oneline -1
```

Expected: a clean tree, then the merge commit (or a fast-forward to `1b8234c`).

---

## Task 1: the frames before -- `Eyes.ReliefLook` at 200 km and at the ground, at 10 and 3 degrees, with their numbers

**Owner:** T. **Depends on:** the merge above. Run this task before Task 2 touches the shared file, so that its frames are the "before".

**Files:**
- Modify: `Source/DeepSpace/Sky/ShipSky.h:378-380` -- `GotoPlacement` gains `double DuskElevation = DuskSunElevation`
- Modify: `Source/DeepSpace/Sky/ShipSky.cpp:800-846` -- `GotoPlacement` uses it
- Modify: `Source/DeepSpace/Tests/ShipSkyTest.cpp` -- new sibling `DeepSpace.Sky.GotoDuskElevation`
- Rewrite: `Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp` (`Eyes.ReliefLook`)
- Create: `Tools/relief_look_compare.py`, `Tools/test_relief_look_compare.py`

**Interfaces:**
- Consumes: `ShipSky::GotoPlacement`, `ShipSky::EGotoSide::Dusk`, `ShipSky::DuskSunElevation`, `ShipGround::FromRelief`, `SkyTestWorld::FSkyWorld`, `SkyTestWorld::PilotEye`, `AWorldGround::FlushBuildsForTest`.
- Produces: `TOptional<FNavPlacement> ShipSky::GotoPlacement(const FSkySystem&, int32 Body, double AltitudeCm, const FUniversePosition& From, EGotoSide Side = EGotoSide::Day, double DuskElevation = DuskSunElevation)`. It also produces `Saved/Eyes/ReliefLook/<tag>/report.txt`, one line per frame: `frame <name> sun <deg> off_mean <x> on_mean <x> coverage <share> off_crc <hex> on_crc <hex>`. Finally, `python3 Tools/relief_look_compare.py <before dir> <after dir>` exits 1 if any shadows-off frame differs.

- [ ] **Step 1: Write the failing test.** Add it to `Source/DeepSpace/Tests/ShipSkyTest.cpp`, after `FShipSkyTest::RunTest`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FGotoDuskElevationTest,
    "DeepSpace.Sky.GotoDuskElevation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Goto dusk at a chosen sun (the cast-shadow plan's frames): the star stands
 * DuskElevation above the ground's horizon under the ship. The default is
 * still DuskSunElevation, ten degrees, which DeepSpace.Sky.ShipSky holds.
 */
bool FGotoDuskElevationTest::RunTest(const FString& Parameters)
{
    const FSkySystem Fixture = SkyTestFixtures::System();
    const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
    const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
    const FVector Sunward = (Star.Position - Home.Position).GetSafeNormal();
    for (const double Degrees : { 3.0, 10.0, 25.0 })
    {
        const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Fixture, SkyTestFixtures::HomeIndex, 1.5e7,
            SkyTestFixtures::Opening(), ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(Degrees));
        if (!TestTrue(FString::Printf(TEXT("goto dusk at %.0f degrees places over a body"), Degrees), Dusk.IsSet()))
        {
            continue;
        }
        const FVector Zenith = (Dusk->Position - Home.Position).GetSafeNormal();
        const double Elevation = FMath::RadiansToDegrees(FMath::Asin(FVector::DotProduct(Zenith, Sunward)));
        TestTrue(FString::Printf(TEXT("the star stands %.0f degrees above the ground's horizon (%.6f)"), Degrees, Elevation),
            FMath::IsNearlyEqual(Elevation, Degrees, 1e-6));
    }
    return true;
}
```

- [ ] **Step 2: Run it to see it fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails. `GotoPlacement` takes no sixth argument ("no matching function for call to 'GotoPlacement'").

- [ ] **Step 3: The elevation.** In `Source/DeepSpace/Sky/ShipSky.h`, replace the declaration:

```cpp
    DEEPSPACE_API TOptional<FNavPlacement> GotoPlacement(const FSkySystem& System, int32 Body,
                                                         double AltitudeCm, const FUniversePosition& From,
                                                         EGotoSide Side = EGotoSide::Day,
                                                         double DuskElevation = DuskSunElevation);
```

Also add to its comment: "`DuskElevation` is the star's height above the horizon under a dusk goto, rad; the frames of the cast-shadow plan take 3 degrees beside the goto's ten." Then in `ShipSky.cpp`, update the definition's signature to match:

```cpp
TOptional<FNavPlacement> ShipSky::GotoPlacement(const FSkySystem& System, int32 Body, double AltitudeCm,
                                                const FUniversePosition& From, EGotoSide Side, double DuskElevation)
```

In the same file, change the dusk branch's two lines:

```cpp
            // The zenith DuskElevation short of square to the star.
            Out = (Aside * FMath::Cos(DuskElevation) + Sunward * FMath::Sin(DuskElevation)).GetSafeNormal();
```

- [ ] **Step 4: Run it to see it pass.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Sky.GotoDuskElevation && ./test.sh 'DeepSpace.Sky.ShipSky$'`

Expected: both PASS.

- [ ] **Step 5: The frames test.** Replace the whole of `Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp` with:

```cpp
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Surface/GroundField.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestWorld.h"
#include "TextureResource.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * FRAMES, NOT A GUARD: Baemsekai III, IV and V under a low sun, for the
 * developer to judge the relief by (sign-off item 2; the ruling on slice
 * (b)'s build, cast shadows). Each world is taken four ways -- from 200 km
 * looking straight down the nose, and from the helm's eye 1.5 m over the
 * ground looking along the terminator, the sun on the side, each at the
 * dusk goto's 10 degrees and at 3 -- and each way twice, with ds.Sky.Shadows
 * 0 and 1. Before the term exists there is no such variable and both are the
 * same frame.
 *
 * Planning measured (the cast-shadow plan) that physical relief casts on
 * under 3% of the ground at ten degrees and on 40-60% at three: the ruled
 * frames are the ten-degree ones, the three-degree ones are where the term
 * is seen.
 *
 *   EYES_TAG=shadows-before Tools/eyes.sh Eyes.ReliefLook
 *   EYES_TAG=shadows-after  Tools/eyes.sh Eyes.ReliefLook
 *   python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after
 *
 * Writes Saved/Eyes/ReliefLook/<tag>/world_<n>_<view>_shadows<0|1>.png and
 * report.txt: per frame the mean brightness without and with the term --
 * Rec. 601 luma, 0-255, exactly PIL's convert("L"), the measure the ruling's
 * 42.9 -> 17.3 was read with -- the share of the ground it shades (of the
 * pixels at 8 or more without it, those it takes under half), and each
 * frame's pixel CRC, so a later run can prove its shadows-off frames are
 * this run's.
 *
 * A fresh editor draws a material as the engine's checkerboard until its
 * shaders compile, hence FinishAllCompilation before each capture.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FReliefLookEyesTest, "Eyes.ReliefLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ReliefLookLocal
{
    struct FView
    {
        const TCHAR* Name;
        double AltitudeCm;     // above the datum from orbit; above the ground itself at the ground
        double SunDegrees;
    };

    const FView Views[] = {
        { TEXT("orbit"), 2.0e7, 10.0 },
        { TEXT("ground"), 150.0, 10.0 },
        { TEXT("orbit_low"), 2.0e7, 3.0 },
        { TEXT("ground_low"), 150.0, 3.0 },
    };

    /** PIL's convert("L"): (R 19595 + G 38470 + B 7471 + 0x8000) >> 16. */
    int32 Luma(const FColor& C)
    {
        return (C.R * 19595 + C.G * 38470 + C.B * 7471 + 0x8000) >> 16;
    }

    double MeanLuma(const TArray<FColor>& Pixels)
    {
        double Sum = 0.0;
        for (const FColor& Pixel : Pixels)
        {
            Sum += Luma(Pixel);
        }
        return Pixels.Num() > 0 ? Sum / Pixels.Num() : 0.0;
    }

    /** Of the pixels lit without the term (luma 8 or more), the share it
     *  takes to under half. */
    double Coverage(const TArray<FColor>& Off, const TArray<FColor>& On)
    {
        int32 Lit = 0;
        int32 Shaded = 0;
        for (int32 Index = 0; Index < Off.Num() && Index < On.Num(); ++Index)
        {
            const int32 Was = Luma(Off[Index]);
            if (Was >= 8)
            {
                ++Lit;
                Shaded += 2 * Luma(On[Index]) < Was ? 1 : 0;
            }
        }
        return Lit > 0 ? static_cast<double>(Shaded) / Lit : 0.0;
    }

    uint32 Crc(const TArray<FColor>& Pixels)
    {
        return FCrc::MemCrc32(Pixels.GetData(), Pixels.Num() * sizeof(FColor));
    }
}

bool FReliefLookEyesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ReliefLookLocal;
    if (GUsingNullRHI)
    {
        AddError(TEXT("Eyes.ReliefLook renders: run it without -nullrhi"));
        return false;
    }
    const FString TagEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("EYES_TAG"));
    const FString Tag = TagEnv.IsEmpty() ? FString(TEXT("untagged")) : TagEnv;
    const FString Dir = FPaths::ProjectSavedDir() / TEXT("Eyes/ReliefLook") / Tag;
    IFileManager::Get().MakeDirectory(*Dir, true);
    // Absent before the term exists: then both passes draw the same frame.
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    const float ShadowsWere = Shadows ? Shadows->GetFloat() : 1.0f;

    FSkyWorld Test(TEXT("ReliefLookWorld"));
    AActor* Camera = Test.World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("a camera"), Camera))
    {
        return false;
    }
    USceneCaptureComponent2D* Capture = NewObject<USceneCaptureComponent2D>(Camera, TEXT("HelmEye"));
    Camera->SetRootComponent(Capture);
    Capture->RegisterComponent();
    UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>(Camera);
    Target->RenderTargetFormat = RTF_RGBA8;
    Target->InitAutoFormat(1920, 1080);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target;
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->FOVAngle = 60.0f;
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Test.BeginPlay();

    TArray<FString> Report;
    for (const int32 Index : { 3, 4, 5 })
    {
        const FSkySystem Here = LocalSystem::Here(Test.World);
        if (!TestTrue(FString::Printf(TEXT("the start system has a body %d"), Index), Here.Bodies.IsValidIndex(Index)))
        {
            return false;
        }
        const FSkyBody& Body = Here.Bodies[Index];
        const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
        for (const FView& View : Views)
        {
            const bool bGround = View.AltitudeCm < 1.0e5;
            const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, Index, bGround ? 0.0 : View.AltitudeCm,
                Test.Ship->GetFlightState().GetUniversePosition(), ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(View.SunDegrees));
            if (!TestTrue(FString::Printf(TEXT("dusk places over body %d"), Index), Dusk.IsSet() && Star))
            {
                return false;
            }
            if (bGround)
            {
                // On the ground under the same sun, looking along the
                // terminator with the star on the side: each hill's shadow
                // falls across the view, not behind it.
                const FVector Up = (Dusk->Position - Body.Position).GetSafeNormal();
                const FVector Sunward = (Star->Position - Body.Position).GetSafeNormal();
                const FVector Heading = FVector::CrossProduct(Up, Sunward).GetSafeNormal();
                const FGroundFieldRef Field = ShipGround::FromRelief(Body.Relief);
                Test.Ship->PlaceShip(Body.Position + Up * (Body.Radius + Field->Height(FVector3d(Up), 0.0) + View.AltitudeCm),
                                     FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
                Capture->SetWorldLocationAndRotation(PilotEye, FRotator(-10.0, 0.0, 0.0));
            }
            else
            {
                // The nose is at the world's centre: straight down.
                Test.Ship->PlaceShip(Dusk->Position, Dusk->Orientation);
                Capture->SetWorldLocationAndRotation(PilotEye, FRotator::ZeroRotator);
            }
            Test.Step(1.0f / 60.0f);
            Test.Ground->FlushBuildsForTest();
            Test.Step(1.0f / 60.0f);

            TArray<FColor> Frames[2];
            for (int32 On = 0; On < 2; ++On)
            {
                if (Shadows)
                {
                    Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
                }
                for (int32 Frame = 0; Frame < 8; ++Frame)
                {
                    if (GShaderCompilingManager)
                    {
                        GShaderCompilingManager->FinishAllCompilation();
                    }
                    Test.Step(1.0f / 60.0f);
                    Test.World->SendAllEndOfFrameUpdates();
                    Capture->CaptureScene();
                }
                Target->GameThread_GetRenderTargetResource()->ReadPixels(Frames[On]);
                const FString Name = FString::Printf(TEXT("world_%d_%s_shadows%d.png"), Index, View.Name, On);
                TUniquePtr<FArchive> File(IFileManager::Get().CreateFileWriter(*(Dir / Name)));
                TestTrue(FString::Printf(TEXT("%s framed"), *Name), File && FImageUtils::ExportRenderTarget2DAsPNG(Target, *File));
            }
            const FString Line = FString::Printf(TEXT("frame world_%d_%s sun %.0f off_mean %.2f on_mean %.2f coverage %.4f off_crc %08x on_crc %08x"),
                Index, View.Name, View.SunDegrees, MeanLuma(Frames[0]), MeanLuma(Frames[1]), Coverage(Frames[0], Frames[1]),
                Crc(Frames[0]), Crc(Frames[1]));
            Report.Add(Line);
            AddInfo(Line);
        }
    }
    if (Shadows)
    {
        Shadows->Set(ShadowsWere, ECVF_SetByCode);
    }
    FFileHelper::SaveStringToFile(FString::Join(Report, TEXT("\n")) + TEXT("\n"), *(Dir / TEXT("report.txt")),
        FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    return true;
}

#endif
```

- [ ] **Step 6: The comparison tool's failing test.** Create `Tools/test_relief_look_compare.py`:

```python
"""Tools/relief_look_compare.py reads two Eyes.ReliefLook reports, and fails
when a frame drawn without the cast shadow differs from the run before it.

    python3 Tools/test_relief_look_compare.py
"""
import os
import tempfile
import unittest

import relief_look_compare as compare

LINE = "frame world_4_orbit sun 10 off_mean 12.70 on_mean %s coverage %s off_crc %s on_crc 0badf00d\n"


def report(directory, *lines):
    os.makedirs(directory, exist_ok=True)
    with open(os.path.join(directory, "report.txt"), "w") as f:
        f.writelines(lines)


class CompareTest(unittest.TestCase):
    def test_reads_every_field(self):
        with tempfile.TemporaryDirectory() as root:
            report(root, LINE % ("12.10", "0.0040", "1a2b3c4d"))
            frames = compare.read(root)
            self.assertEqual(frames["world_4_orbit"]["on_mean"], "12.10")
            self.assertEqual(frames["world_4_orbit"]["off_crc"], "1a2b3c4d")

    def test_same_off_frames_pass(self):
        with tempfile.TemporaryDirectory() as root:
            report(os.path.join(root, "a"), LINE % ("12.70", "0.0000", "1a2b3c4d"))
            report(os.path.join(root, "b"), LINE % ("12.10", "0.0040", "1a2b3c4d"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 0)

    def test_a_changed_off_frame_fails(self):
        with tempfile.TemporaryDirectory() as root:
            report(os.path.join(root, "a"), LINE % ("12.70", "0.0000", "1a2b3c4d"))
            report(os.path.join(root, "b"), LINE % ("12.10", "0.0040", "ffffffff"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 1)

    def test_a_frame_missing_before_fails(self):
        with tempfile.TemporaryDirectory() as root:
            report(os.path.join(root, "a"))
            report(os.path.join(root, "b"), LINE % ("12.10", "0.0040", "1a2b3c4d"))
            self.assertEqual(compare.main(os.path.join(root, "a"), os.path.join(root, "b")), 1)


if __name__ == "__main__":
    unittest.main()
```

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_relief_look_compare.py`

Expected: ERROR, `ModuleNotFoundError: No module named 'relief_look_compare'`.

- [ ] **Step 7: The tool.** Create `Tools/relief_look_compare.py`:

```python
"""Eyes.ReliefLook's two runs side by side: each frame's mean brightness
before the cast shadow existed, without it now, and with it, and the share of
the ground it shades. Fails if any frame drawn without the term is not
pixel for pixel the frame of the run before (the frames' CRCs): ds.Sky.Shadows
0 must be exactly the old look.

    python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after
"""
import os
import sys


def read(directory):
    """{frame name: {field: value}} from a report's `frame` lines."""
    frames = {}
    with open(os.path.join(directory, "report.txt")) as f:
        for line in f:
            fields = line.split()
            if len(fields) < 2 or fields[0] != "frame":
                continue
            frames[fields[1]] = dict(zip(fields[2::2], fields[3::2]))
    return frames


def main(before_dir, after_dir):
    before, after = read(before_dir), read(after_dir)
    failed = False
    print("%-20s %4s %8s %8s %8s %9s" % ("frame", "sun", "before", "off", "on", "coverage"))
    for name in sorted(after):
        now = after[name]
        was = before.get(name)
        same = was is not None and was["off_crc"] == now["off_crc"]
        failed = failed or not same
        print("%-20s %4s %8s %8s %8s %8.1f%%%s" % (
            name, now["sun"], was["off_mean"] if was else "-", now["off_mean"], now["on_mean"],
            100.0 * float(now["coverage"]), "" if same else "  OFF FRAME DIFFERS"))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
```

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t/Tools && python3 test_relief_look_compare.py`

Expected: 4 tests OK.

- [ ] **Step 8: Take the frames before the term.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && EYES_TAG=shadows-before Tools/eyes.sh Eyes.ReliefLook && cat Saved/Eyes/ReliefLook/shadows-before/report.txt`

Expected: `passed: 1`, 24 PNGs, and 12 `frame` lines. Every line's `off_crc` equals its `on_crc` and its coverage is 0.0000, because the variable does not exist yet. The three `orbit` lines' `off_mean` are within 0.5 of the ruling's "after" numbers: 17.3 (III), 2.0 (IV) and 4.5 (V). That is the same view T2's frames were taken from. A larger gap means the view moved, so stop and find out why before going on.

- [ ] **Step 9: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Sky/ShipSky.h Source/DeepSpace/Sky/ShipSky.cpp \
  Source/DeepSpace/Tests/ShipSkyTest.cpp Source/DeepSpace/Tests/Eyes/ReliefLookEyesTest.cpp \
  Tools/relief_look_compare.py Tools/test_relief_look_compare.py && \
git commit -qm "test(sky): Eyes.ReliefLook takes the ground and a 3-degree sun beside the dusk goto, with brightness, coverage and CRCs

Goto dusk takes the star's elevation (DeepSpace.Sky.GotoDuskElevation). The
frames before the cast shadow are in Saved/Eyes/ReliefLook/shadows-before.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 10: Prove the new test.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'Out = (Aside * FMath::Cos(DuskElevation)' 'Out = (Aside * FMath::Cos(DuskSunElevation)' 'DeepSpace.Sky.GotoDuskElevation$'; ./build.sh`

Expected: `KILLED`. At 3 and 25 degrees the zenith is no longer the one asked for.

---

## Task 2: `WR_SunVisible` in the shared file, and its C++ mirror

**Owner:** T. **Depends on:** Task 1 (its frames are taken).

**Files:**
- Modify: `Shaders/Private/WorldRelief.ush`. Add six shims and `WR_LOOP` to each half (after line 57 in C++, after line 72 in HLSL), and the cast shadow at the end of the file.
- Modify: `Source/DeepSpace/Surface/WorldRelief.h` (`FSunVisibility`; `WorldReliefNoise::{SimplexValue, SimplexValueF32, SimplexF32, PeakCap, ShadowHeight, DiscAbove, SunVisibleF64, SunVisibleF32, FShadowConstants, ShadowConstants}`; `WorldReliefShading::SunVisible`)
- Modify: `Source/DeepSpace/Surface/WorldRelief.cpp`
- Create: `Source/DeepSpace/Tests/WorldReliefShadowTest.cpp`, containing `DeepSpace.Surface.WorldRelief.ShadowHeight`, `.ShadowGradientMax`, `.SunDisc`, `.ShadowAgainstTruth` and `.ShadowCost`

**Interfaces:**
- Consumes: the shared file's `WR_Rand3DPCG16`, `WR_GradientDirection`, `WR_SplitOffset`, `WR_FloorDiv3`, `WR_DETAIL_FREQUENCY`, `WR_DETAIL_WEIGHT`, `WR_DETAIL_INDEX`, `WR_SIMPLEX_SCALE`, `WR_DETAIL_BANDS`. From C++, `FWorldRelief::{SMaxMeasured, PeakCapKnee, PeakCap, Height, SlopeScale}`, `WorldReliefNoise::{Simplex, FaceF64, Bands}`.
- Produces, in the shared file: `WR_Shadow {Visible, Horizon, Bands}` and `WR_Shadow WR_SunVisible(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL LX, WR_REAL LY, WR_REAL LZ, WR_REAL Footprint, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL ReliefScale, WR_REAL SunRadius, WR_REAL Samples)`. Also `WR_ShadowHeight`, `WR_DetailHeight`, `WR_SimplexValueAt`, `WR_PeakCap`, `WR_DiscAbove`, and the constants `WR_SHADOW_FOOTPRINT_FACTOR` (2.0), `WR_S_MAX`, `WR_PEAK_CAP_KNEE`, `WR_SHADOW_GRADIENT_MAX` (21.3) and `WR_PI`.
- Produces, in C++:

```cpp
struct FSunVisibility { double Visible = 1.0; double Horizon = -UE_DOUBLE_HALF_PI; int32 Bands = 0; };
FSunVisibility WorldReliefNoise::SunVisibleF64(const FVector3d& D, const FVector3d& L, double FootprintD, const FVector3d& Offset,
                                               double ReliefScale, double SunRadius, int32 Samples);
FSunVisibility WorldReliefNoise::SunVisibleF32(const FVector3f& D, const FVector3f& L, float FootprintD, const FVector3f& Offset,
                                               float ReliefScale, float SunRadius, int32 Samples);
FSunVisibility WorldReliefShading::SunVisible(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& L,
                                              double SunRadius, double FootprintRadius, int32 Samples);
double WorldReliefNoise::ShadowHeight(const FVector3d& D, double FootprintD, const FVector3d& Offset, double PeakR);
double WorldReliefNoise::DiscAbove(double X);
double WorldReliefNoise::PeakCap(double X);
double WorldReliefNoise::SimplexValue(const FVector3d& V);
float WorldReliefNoise::SimplexValueF32(const FVector3f& V);
float WorldReliefNoise::SimplexF32(const FVector3f& V);
struct WorldReliefNoise::FShadowConstants { double SMax, PeakCapKnee, GradientMax, FootprintFactor; };
WorldReliefNoise::FShadowConstants WorldReliefNoise::ShadowConstants();
```

- [ ] **Step 1: Write the failing tests.** Create `Source/DeepSpace/Tests/WorldReliefShadowTest.cpp`:

```cpp
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/WorldRelief.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefShadowHeightTest, "DeepSpace.Surface.WorldRelief.ShadowHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefShadowGradientMaxTest, "DeepSpace.Surface.WorldRelief.ShadowGradientMax",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefSunDiscTest, "DeepSpace.Surface.WorldRelief.SunDisc",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefShadowAgainstTruthTest, "DeepSpace.Surface.WorldRelief.ShadowAgainstTruth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefShadowCostTest, "DeepSpace.Surface.WorldRelief.ShadowCost",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefShadowTestLocal
{
    /** Baemsekai IV's radius and relief (Saved/procgen_corpus.tsv: 0.896805
     *  Earth radii, 5.47511 km), and its star's angular radius from there
     *  (0.16 solar radii seen from 0.0256 AU). The seed offsets are drawn:
     *  the noise is the same everywhere, so any offset is fair ground. */
    constexpr double FourthRadiusCm = 0.896805 * 6.3781e8;
    constexpr double FourthPeakCm = 5.47511e5;
    constexpr double FourthSunRadius = 0.02903;
    /** Baemsekai V's: 0.843852 Earth radii, 6.12194 km, the star 0.97
     *  degrees in radius. */
    constexpr double FifthRadiusCm = 0.843852 * 6.3781e8;
    constexpr double FifthPeakCm = 6.12194e5;
    constexpr double FifthSunRadius = 0.01700;
    /** The pixel footprint from 200 km, radius units: about 110 m a pixel
     *  on IV at 1920 across 60 degrees, times filter_pixels. */
    constexpr double OrbitFootprint = 4.0e-5;
    /** The sample count the materials use (SkyMaterial::ShadowSamples; the
     *  Surface tests do not reach the Sky's contract). DeepSpace.Sky.
     *  MaterialContract holds the JSON to the header, and Task 4's gate
     *  edits all three together. */
    constexpr int32 Samples = 12;
    /** The patch centre and east of the parity probe, an orthonormal pair. */
    const FVector3d Centre(0.6, -0.48, 0.64);
    const FVector3d East(0.8, 0.36, -0.48);

    FWorldReliefParams Ground(double RadiusCm, double PeakCm, const FVector3d& Offset)
    {
        FWorldReliefParams Params;
        Params.SeedOffset = Offset;
        Params.RadiusCm = RadiusCm;
        Params.PeakCm = PeakCm;
        Params.Cratering = 0.0;
        Params.Ground = EGround::Solid;
        return Params;
    }

    /** A seed offset as the sky's are: three multiples of 1/256 under 256,
     *  drawn X then Y then Z. */
    FVector3d OffsetFrom(FRandomStream& Stream)
    {
        const int32 X = Stream.RandRange(0, 65535);
        const int32 Y = Stream.RandRange(0, 65535);
        const int32 Z = Stream.RandRange(0, 65535);
        return FVector3d(X, Y, Z) / 256.0;
    }

    /** The star Elevation rad above D's horizon, toward Toward's part along it. */
    FVector3d LightAt(const FVector3d& D, const FVector3d& Toward, double Elevation)
    {
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        return (D * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
    }

    /** The steepest horizon the march can find on this ground, rad. */
    double Steepest(const FWorldReliefParams& Params)
    {
        return FMath::Atan(FWorldRelief(Params).SlopeScale() * WorldReliefNoise::ShadowConstants().GradientMax);
    }

    /** The truth the march approximates: the same heights at the pixel's own
     *  footprint, a step an eighth of the finest band that footprint keeps,
     *  out to the same end, under the same disc. */
    double Truth(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& L, double SunRadius, double Footprint)
    {
        const double PeakR = Params.PeakCm / Params.RadiusCm;
        const double SinT = FVector3d::DotProduct(L, D);
        const FVector3d Along = (L - D * SinT).GetSafeNormal();
        const double Theta = FMath::Asin(SinT);
        const double Here = WorldReliefNoise::ShadowHeight(D, Footprint, Params.SeedOffset, PeakR);
        const double Lower = FMath::Tan(Theta - SunRadius);
        const double End = FMath::Sqrt(Lower * Lower + 2.0 * FMath::Max(0.0, PeakR - Here)) - Lower;
        double Finest = WorldReliefNoise::Bands().DetailFrequencies[0];
        for (const double Frequency : WorldReliefNoise::Bands().DetailFrequencies)
        {
            Finest = Footprint * Frequency < 1.0 ? Frequency : Finest;
        }
        const double Step = 1.0 / (8.0 * Finest);
        double Highest = -1.0e9;
        for (double S = Step; S <= End + Step; S += Step)
        {
            const FVector3d P = (D + Along * S).GetSafeNormal();
            const double There = WorldReliefNoise::ShadowHeight(P, Footprint, Params.SeedOffset, PeakR);
            Highest = FMath::Max(Highest, (There - Here - 0.5 * S * S) / S);
        }
        return WorldReliefNoise::DiscAbove((FMath::Atan(Highest) - Theta) / SunRadius);
    }
}

bool FWorldReliefShadowHeightTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefShadowTestLocal;
    const WorldReliefNoise::FShadowConstants Constants = WorldReliefNoise::ShadowConstants();
    TestEqual(TEXT("the file's S_max is FWorldRelief's measured maximum"), Constants.SMax, FWorldRelief::SMaxMeasured);
    TestEqual(TEXT("and its cap's knee is FWorldRelief's"), Constants.PeakCapKnee, FWorldRelief::PeakCapKnee);
    TestEqual(TEXT("a sample's footprint is twice its spacing"), Constants.FootprintFactor, 2.0);

    // The cap is FWorldRelief's, value for value, above the knee too.
    double WorstCap = 0.0;
    for (int32 Step = -300; Step <= 300; ++Step)
    {
        const double X = Step / 200.0;
        WorstCap = FMath::Max(WorstCap, FMath::Abs(WorldReliefNoise::PeakCap(X) - FWorldRelief::PeakCap(X)));
    }
    TestTrue(FString::Printf(TEXT("WR_PeakCap is FWorldRelief::PeakCap from -1.5 to 1.5 (%.2e)"), WorstCap), WorstCap <= 1.0e-15);

    // The value alone is the simplex's value, in double and in float.
    FRandomStream Stream(20260928);
    double WorstDouble = 0.0;
    double WorstFloat = 0.0;
    for (int32 Sample = 0; Sample < 20000; ++Sample)
    {
        const FVector3d V(Stream.FRandRange(-3000.0, 3000.0), Stream.FRandRange(-3000.0, 3000.0), Stream.FRandRange(-3000.0, 3000.0));
        FVector3d Gradient;
        WorstDouble = FMath::Max(WorstDouble, FMath::Abs(WorldReliefNoise::SimplexValue(V) - WorldReliefNoise::Simplex(V, Gradient)));
        const FVector3f F(V);
        WorstFloat = FMath::Max(WorstFloat, static_cast<double>(FMath::Abs(WorldReliefNoise::SimplexValueF32(F) - WorldReliefNoise::SimplexF32(F))));
    }
    TestTrue(FString::Printf(TEXT("WR_SimplexValueAt is WR_SimplexAt's value in double (%.2e)"), WorstDouble), WorstDouble <= 1.0e-14);
    TestTrue(FString::Printf(TEXT("and in float (%.2e)"), WorstFloat), WorstFloat <= 1.0e-6);

    // The march's height is the ground's -- craters aside, and wherever the
    // C++'s finer bands have faded -- at every footprint.
    const FWorldReliefParams Params = Ground(FourthRadiusCm, FourthPeakCm, OffsetFrom(Stream));
    const FWorldRelief Relief(Params);
    const double PeakR = Params.PeakCm / Params.RadiusCm;
    for (const double Footprint : { 1.0 / 12.0, 1.0 / 96.0, 1.0 / 768.0, 1.0 / 3072.0, 1.0 / 12288.0, 1.0 / 49152.0, 1.0 / 98304.0 })
    {
        double Worst = 0.0;
        for (int32 Sample = 0; Sample < 4000; ++Sample)
        {
            const FVector3d D(Stream.GetUnitVector());
            const double Mine = WorldReliefNoise::ShadowHeight(D, Footprint, Params.SeedOffset, PeakR) * Params.RadiusCm;
            Worst = FMath::Max(Worst, FMath::Abs(Mine - Relief.Height(D, Footprint * Params.RadiusCm)));
        }
        TestTrue(FString::Printf(TEXT("WR_ShadowHeight is FWorldRelief::Height at footprint 1/%.0f, to 1e-3 cm (%.2e cm)"), 1.0 / Footprint, Worst),
            Worst <= 1.0e-3);
    }
    return true;
}

bool FWorldReliefShadowGradientMaxTest::RunTest(const FString& Parameters)
{
    // WR_SHADOW_GRADIENT_MAX's recipe: 256 worlds' offsets and 1,024
    // directions each, from one FRandomStream(20260928); the twelve GPU
    // bands' |grad S| along the ground (FaceF64's DetailSlope at footprint 0).
    // Planning measured 14.19 with another generator; the constant is 1.5 x.
    FRandomStream Stream(20260928);
    double Measured = 0.0;
    for (int32 World = 0; World < 256; ++World)
    {
        const FVector3d Offset = WorldReliefShadowTestLocal::OffsetFrom(Stream);
        for (int32 Sample = 0; Sample < 1024; ++Sample)
        {
            const FVector3d D(Stream.GetUnitVector());
            const FVector3d G = WorldReliefNoise::FaceF64(D, 0.0, Offset, 1.0).DetailSlope;
            Measured = FMath::Max(Measured, (G - D * FVector3d::DotProduct(G, D)).Size());
        }
    }
    const double Constant = WorldReliefNoise::ShadowConstants().GradientMax;
    TestTrue(FString::Printf(TEXT("WR_SHADOW_GRADIENT_MAX (%.2f) is 1.4 to 1.7 times the steepest of 262,144 samples (%.3f): the exit never cuts a horizon and still spares the day side"),
        Constant, Measured), Constant >= 1.4 * Measured && Constant <= 1.7 * Measured);
    return true;
}

bool FWorldReliefSunDiscTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefShadowTestLocal;
    // The disc above a straight horizon.
    TestTrue(TEXT("a horizon a radius below the disc's centre leaves it whole"), WorldReliefNoise::DiscAbove(-1.0) == 1.0);
    TestTrue(TEXT("one at its centre leaves half"), FMath::IsNearlyEqual(WorldReliefNoise::DiscAbove(0.0), 0.5, 1e-15));
    TestTrue(TEXT("one a radius above hides it"), WorldReliefNoise::DiscAbove(1.0) == 0.0);
    TestTrue(TEXT("and past either edge it stays there"), WorldReliefNoise::DiscAbove(-7.0) == 1.0 && WorldReliefNoise::DiscAbove(7.0) == 0.0);
    double Before = 2.0;
    for (int32 Step = -10; Step <= 10; ++Step)
    {
        const double X = Step / 10.0;
        const double Share = WorldReliefNoise::DiscAbove(X);
        TestTrue(FString::Printf(TEXT("symmetric at %.1f"), X), FMath::IsNearlyEqual(Share, 1.0 - WorldReliefNoise::DiscAbove(-X), 1e-12));
        TestTrue(FString::Printf(TEXT("and falling at %.1f"), X), Share < Before);
        Before = Share;
    }

    const FVector3d D = Centre.GetSafeNormal();
    const FWorldReliefParams Fourth = Ground(FourthRadiusCm, FourthPeakCm, FVector3d(12.5, 200.25, 77.0));
    const double R = FourthSunRadius;
    const double Cut = Steepest(Fourth) + R;
    const auto At = [&](const FWorldReliefParams& Params, double Elevation)
    {
        return WorldReliefShading::SunVisible(Params, D, LightAt(D, East, Elevation), R, OrbitFootprint, Samples);
    };

    // The exits meet the march with no step.
    const FSunVisibility Over = At(Fourth, Cut + 1.0e-3);
    TestTrue(TEXT("above the steepest ground the disc is whole, unmarched"), Over.Visible == 1.0 && Over.Bands == 0);
    const FSunVisibility JustUnder = At(Fourth, Cut - 0.5 * R);
    TestTrue(FString::Printf(TEXT("between the steepest slope and it plus the sun's radius the march runs (%d bands)"), JustUnder.Bands),
        JustUnder.Bands > 0);
    TestTrue(FString::Printf(TEXT("and finds the disc whole there too (%.6f)"), JustUnder.Visible), JustUnder.Visible >= 0.999);
    const FSunVisibility Under = At(Fourth, -Cut - 1.0e-3);
    TestTrue(TEXT("below the steepest ground's negative the disc is hidden, unmarched"), Under.Visible == 0.0 && Under.Bands == 0);
    const FSunVisibility JustOver = At(Fourth, -Cut + 0.5 * R);
    TestTrue(FString::Printf(TEXT("and just inside it the march runs (%d bands) and finds it hidden (%.6f)"), JustOver.Bands, JustOver.Visible),
        JustOver.Bands > 0 && JustOver.Visible <= 1.0e-3);

    // The sun at the zenith: no azimuth to march along.
    const FSunVisibility Zenith = WorldReliefShading::SunVisible(Fourth, D, D, R, OrbitFootprint, Samples);
    TestTrue(TEXT("the sun overhead is whole and finite"), Zenith.Visible == 1.0 && FMath::IsFinite(Zenith.Horizon));

    // No ground: an ocean or a giant casts nothing.
    FWorldReliefParams Sea = Fourth;
    Sea.Ground = EGround::None;
    const FSunVisibility Wet = At(Sea, FMath::DegreesToRadians(2.0));
    TestTrue(TEXT("a world with no ground casts no shadow and marches nothing"), Wet.Visible == 1.0 && Wet.Bands == 0);

    // A world with 1 cm of relief: the only horizon is the sphere's.
    const FWorldReliefParams Smooth = Ground(FourthRadiusCm, 1.0, FVector3d(12.5, 200.25, 77.0));
    TestTrue(TEXT("on smooth ground the sun two radii up is whole"), At(Smooth, 2.0 * R).Visible >= 0.999);
    TestTrue(TEXT("two radii down it is hidden"), At(Smooth, -2.0 * R).Visible <= 0.001);
    const FSunVisibility Level = At(Smooth, 0.0);
    TestTrue(FString::Printf(TEXT("its centre on the level shows half (%.4f), finite"), Level.Visible),
        FMath::IsFinite(Level.Visible) && FMath::Abs(Level.Visible - 0.5) <= 0.01);
    double Rising = -1.0;
    for (int32 Step = -10; Step <= 10; ++Step)
    {
        const double Visible = At(Smooth, Step * 0.2 * R).Visible;
        TestTrue(FString::Printf(TEXT("and it rises with the sun (%.2f r: %.4f)"), Step * 0.2, Visible), Visible >= Rising);
        Rising = Visible;
    }
    return true;
}

bool FWorldReliefShadowAgainstTruthTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefShadowTestLocal;
    // Planning's measure, in the engine: at the orbit's footprint the march
    // shades 0.79-0.91 of what the truth shades at 5 and 2 degrees, never more,
    // mean |dv| 0.06 at 5 and 0.10 at 2; the footprint rule's fade is the
    // price of never aliasing. Held with room: the shadowed share within
    // 0.6-1.05 of the truth's, the penumbra's within 0.5-2, mean |dv| under
    // 0.12 at 5 degrees and 0.16 at 2.
    struct FCase { const TCHAR* Name; double RadiusCm; double PeakCm; double SunRadius; double Degrees; double MeanGap; };
    const FCase Cases[] = {
        { TEXT("Baemsekai IV, 5 degrees"), FourthRadiusCm, FourthPeakCm, FourthSunRadius, 5.0, 0.12 },
        { TEXT("Baemsekai IV, 2 degrees"), FourthRadiusCm, FourthPeakCm, FourthSunRadius, 2.0, 0.16 },
        { TEXT("Baemsekai V, 5 degrees"), FifthRadiusCm, FifthPeakCm, FifthSunRadius, 5.0, 0.12 },
        { TEXT("Baemsekai V, 2 degrees"), FifthRadiusCm, FifthPeakCm, FifthSunRadius, 2.0, 0.16 },
    };
    FRandomStream Stream(20260929);
    for (const FCase& Case : Cases)
    {
        constexpr int32 Count = 200;
        int32 MarchShaded = 0;
        int32 TruthShaded = 0;
        int32 MarchPenumbra = 0;
        int32 TruthPenumbra = 0;
        double Gap = 0.0;
        for (int32 Sample = 0; Sample < Count; ++Sample)
        {
            const FWorldReliefParams Params = Ground(Case.RadiusCm, Case.PeakCm, OffsetFrom(Stream));
            const FVector3d D(Stream.GetUnitVector());
            const FVector3d L = LightAt(D, FVector3d(Stream.GetUnitVector()), FMath::DegreesToRadians(Case.Degrees));
            const double March = WorldReliefShading::SunVisible(Params, D, L, Case.SunRadius, OrbitFootprint, Samples).Visible;
            const double Truth = WorldReliefShadowTestLocal::Truth(Params, D, L, Case.SunRadius, OrbitFootprint);
            MarchShaded += March < 0.5 ? 1 : 0;
            TruthShaded += Truth < 0.5 ? 1 : 0;
            MarchPenumbra += March > 0.01 && March < 0.99 ? 1 : 0;
            TruthPenumbra += Truth > 0.01 && Truth < 0.99 ? 1 : 0;
            Gap += FMath::Abs(March - Truth);
        }
        const double Ratio = TruthShaded > 0 ? static_cast<double>(MarchShaded) / TruthShaded : 0.0;
        const double PenumbraRatio = TruthPenumbra > 0 ? static_cast<double>(MarchPenumbra) / TruthPenumbra : 0.0;
        AddInfo(FString::Printf(TEXT("%s: shaded %d against the truth's %d (%.2f), penumbra %d against %d, mean |dv| %.3f"),
            Case.Name, MarchShaded, TruthShaded, Ratio, MarchPenumbra, TruthPenumbra, Gap / Count));
        TestTrue(FString::Printf(TEXT("%s: the march shades 0.6 to 1.05 of the truth's ground (%.2f)"), Case.Name, Ratio),
            TruthShaded >= 10 && Ratio >= 0.6 && Ratio <= 1.05);
        TestTrue(FString::Printf(TEXT("%s: its penumbra is the sun's, 0.5 to 2 of the truth's (%.2f)"), Case.Name, PenumbraRatio),
            PenumbraRatio >= 0.5 && PenumbraRatio <= 2.0);
        TestTrue(FString::Printf(TEXT("%s: mean |dv| under %.2f (%.3f)"), Case.Name, Case.MeanGap, Gap / Count), Gap / Count <= Case.MeanGap);
    }
    return true;
}

bool FWorldReliefShadowCostTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefShadowTestLocal;
    // What a marching pixel costs, in band evaluations: planning measured 98
    // at 12 samples on Baemsekai IV under a 10-degree sun from 200 km (about
    // 8.2 a sample), 187 with the footprint rule taken out. The budget gate
    // (Task 4) is priced in these, so they are pinned: at most 9.2 and at
    // least 4 a sample.
    FRandomStream Stream(20260930);
    double Bands = 0.0;
    constexpr int32 Count = 300;
    for (int32 Sample = 0; Sample < Count; ++Sample)
    {
        const FWorldReliefParams Params = Ground(FourthRadiusCm, FourthPeakCm, OffsetFrom(Stream));
        const FVector3d D(Stream.GetUnitVector());
        const FVector3d L = LightAt(D, FVector3d(Stream.GetUnitVector()), FMath::DegreesToRadians(10.0));
        Bands += WorldReliefShading::SunVisible(Params, D, L, FourthSunRadius, OrbitFootprint, Samples).Bands;
    }
    const double PerPixel = Bands / Count;
    TestTrue(FString::Printf(TEXT("a marching pixel reads 4 to 9.2 bands a sample (%.1f at %d samples)"), PerPixel, Samples),
        PerPixel >= 4.0 * Samples && PerPixel <= 9.2 * Samples);
    return true;
}

#endif
```

- [ ] **Step 2: Run them to see them fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails. `FSunVisibility`, `WorldReliefNoise::ShadowConstants` and the rest are undeclared.

- [ ] **Step 3: The shims.** In `Shaders/Private/WorldRelief.ush`, add these to the C++ half, after `WR_step`'s line:

```cpp
inline WR_REAL WR_abs(WR_REAL X) { return X < WR_REAL(0.0) ? -X : X; }
inline WR_REAL WR_tanh(WR_REAL X) { return std::tanh(X); }
inline WR_REAL WR_atan(WR_REAL X) { return std::atan(X); }
inline WR_REAL WR_tan(WR_REAL X) { return std::tan(X); }
inline WR_REAL WR_acos(WR_REAL X) { return std::acos(X); }
inline WR_REAL WR_pow(WR_REAL X, WR_REAL Y) { return std::pow(X, Y); }
#define WR_LOOP
```

Add these to the HLSL half, after `#define WR_step(Edge, X) step(Edge, X)`:

```hlsl
#define WR_abs(X) abs(X)
#define WR_tanh(X) tanh(X)
#define WR_atan(X) atan(X)
#define WR_tan(X) tan(X)
#define WR_acos(X) acos(X)
#define WR_pow(X, Y) pow(X, Y)
// A loop the compiler must keep a loop: its trip count is a parameter, and it breaks.
#define WR_LOOP [loop]
```

- [ ] **Step 4: The cast shadow.** Append this to the end of `Shaders/Private/WorldRelief.ush`, after `WR_GroundNormal`:

```hlsl
// -- Cast shadows (the developer's ruling on slice (b)'s build, 2026-09-28) --
// How much of the star's disc a point of the ground sees past the ground
// toward it: a march through the height function toward the sun, used by
// M_SkyBody and M_SkyGround alike, in the orbit and on the ground, so the
// 50 km handover keeps it. Held to the C++ by Eyes.WorldReliefParity.
//
// The height it reads is FWorldRelief::Height over the radius, from the
// GPU's twelve detail bands (WR_ShadowHeight). Craters are not marched:
// their walls are at most 2 x crater_depth x ReliefScale steep -- 1.5
// degrees on Baemsekai IV -- so they cast nothing under a sun above that,
// and a crater band is 27 cells of hashes, which the budget cannot pay
// twelve times over.
//
// The march: Samples samples (the contract's shadow_samples) along the
// ground toward the sun's azimuth, geometric from the pixel (never nearer
// than half the finest band's wavelength) to where the sun's lower edge
// clears the highest peak the world can have (PeakCm) over the curving
// ground. Each sample reads the bands at WR_SHADOW_FOOTPRINT_FACTOR times
// its own spacing, the face's own fade -- filter_pixels' rule -- so no band
// is read at under two samples a cycle and the march never aliases. Each
// is measured against the pixel's own height at the pixel's footprint.
//
// The shadow's softness is the sun's size: the share of a disc of angular
// radius SunRadius above the highest horizon the samples found.
static const WR_REAL WR_SHADOW_FOOTPRINT_FACTOR = WR_REAL(2.0);
static const WR_REAL WR_S_MAX = WR_REAL(0.029932993600784347);  // FWorldRelief::SMaxMeasured
static const WR_REAL WR_PEAK_CAP_KNEE = WR_REAL(0.8);            // FWorldRelief::PeakCapKnee
// The steepest |grad S| of the twelve bands along the ground, 1.5 x the
// 14.19 of 262,144 samples: no horizon the march can find is steeper than
// atan(ReliefScale x this). DeepSpace.Surface.WorldRelief.ShadowGradientMax.
static const WR_REAL WR_SHADOW_GRADIENT_MAX = WR_REAL(21.3);
static const WR_REAL WR_PI = WR_REAL(3.14159265358979323846);

// FWorldRelief::PeakCap, value only.
WR_REAL WR_PeakCap(WR_REAL X)
{
    const WR_REAL Magnitude = WR_abs(X);
    if (Magnitude <= WR_PEAK_CAP_KNEE)
    {
        return X;
    }
    const WR_REAL Room = WR_REAL(1.0) - WR_PEAK_CAP_KNEE;
    const WR_REAL Capped = WR_min(WR_REAL(1.0), WR_PEAK_CAP_KNEE + Room * WR_tanh((Magnitude - WR_PEAK_CAP_KNEE) / Room));
    return X < WR_REAL(0.0) ? -Capped : Capped;
}

// WR_SimplexCorner's value alone, operation for operation.
WR_REAL WR_SimplexCornerValue(WR_REAL VX, WR_REAL VY, WR_REAL VZ, WR_REAL TX, WR_REAL TY, WR_REAL TZ,
                              int HX, int HY, int HZ)
{
    const WR_REAL FX = VX - TX;
    const WR_REAL FY = VY - TY;
    const WR_REAL FZ = VZ - TZ;
    const WR_Vec3 G = WR_GradientDirection(WR_Rand3DPCG16(
        int(WR_floor(WR_REAL(6.0) * TX + WR_REAL(0.5))) + HX,
        int(WR_floor(WR_REAL(6.0) * TY + WR_REAL(0.5))) + HY,
        int(WR_floor(WR_REAL(6.0) * TZ + WR_REAL(0.5))) + HZ).X);
    const WR_REAL GF = G.X * FX + G.Y * FY + G.Z * FZ;
    const WR_REAL D2 = FX * FX + FY * FY + FZ * FZ;
    const WR_REAL S = WR_saturate(WR_REAL(2.0) * D2);
    const WR_REAL Smooth = WR_SIMPLEX_SCALE + S * (WR_REAL(-3.0) * WR_SIMPLEX_SCALE + S * (WR_REAL(3.0) * WR_SIMPLEX_SCALE - S * WR_SIMPLEX_SCALE));
    return Smooth * GF;
}

// WR_SimplexAt's value alone: the same corners, in the same order.
WR_REAL WR_SimplexValueAt(WR_REAL VX, WR_REAL VY, WR_REAL VZ, int ShiftX, int ShiftY, int ShiftZ)
{
    const WR_REAL TX = WR_floor(VX + VX / WR_REAL(3.0) + VY / WR_REAL(3.0) + VZ / WR_REAL(3.0));
    const WR_REAL TY = WR_floor(VY + VX / WR_REAL(3.0) + VY / WR_REAL(3.0) + VZ / WR_REAL(3.0));
    const WR_REAL TZ = WR_floor(VZ + VX / WR_REAL(3.0) + VY / WR_REAL(3.0) + VZ / WR_REAL(3.0));
    const WR_REAL BX = TX - TX / WR_REAL(6.0) - TY / WR_REAL(6.0) - TZ / WR_REAL(6.0);
    const WR_REAL BY = TY - TX / WR_REAL(6.0) - TY / WR_REAL(6.0) - TZ / WR_REAL(6.0);
    const WR_REAL BZ = TZ - TX / WR_REAL(6.0) - TY / WR_REAL(6.0) - TZ / WR_REAL(6.0);
    const WR_REAL FX = VX - BX;
    const WR_REAL FY = VY - BY;
    const WR_REAL FZ = VZ - BZ;
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
    WR_REAL Value = WR_REAL(0.0);
    Value += WR_SimplexCornerValue(VX, VY, VZ, BX, BY, BZ, ShiftX, ShiftY, ShiftZ);
    Value += WR_SimplexCornerValue(VX, VY, VZ, BX + A1X, BY + A1Y, BZ + A1Z, ShiftX, ShiftY, ShiftZ);
    Value += WR_SimplexCornerValue(VX, VY, VZ, BX + A2X, BY + A2Y, BZ + A2Z, ShiftX, ShiftY, ShiftZ);
    Value += WR_SimplexCornerValue(VX, VY, VZ, BX + WR_REAL(0.5), BY + WR_REAL(0.5), BZ + WR_REAL(0.5), ShiftX, ShiftY, ShiftZ);
    return Value;
}

// One detail band's height, unfaded, radius units: WR_DetailBand's value at
// no footprint, times the band's weight, over its frequency.
WR_REAL WR_DetailHeight(WR_REAL DX, WR_REAL DY, WR_REAL DZ, int Band, WR_REAL OX, WR_REAL OY, WR_REAL OZ)
{
    const WR_REAL F = WR_DETAIL_FREQUENCY[Band];
    const WR_Offset O = WR_SplitOffset(OX, OY, OZ, WR_DETAIL_INDEX[Band]);
    const int Sum = O.IX + O.IY + O.IZ;
    const int Q = WR_FloorDiv3(Sum);
    const WR_REAL Left = WR_REAL(Sum - 3 * Q) / WR_REAL(6.0);
    const WR_REAL Value = WR_SimplexValueAt(DX * F + O.FX + Left, DY * F + O.FY + Left, DZ * F + O.FZ + Left,
                                            6 * O.IX + 3 * Q - Sum, 6 * O.IY + 3 * Q - Sum, 6 * O.IZ + 3 * Q - Sum);
    return Value * WR_DETAIL_WEIGHT[Band] / F;
}

// The height the march reads at D, radius units above the datum, at a
// footprint (radius units): PeakR x PeakCap(S / S_max), S the twelve bands
// each faded by saturate(1 - footprint x frequency). PeakR is PeakCm / R.
WR_REAL WR_ShadowHeight(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL Footprint, WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL PeakR)
{
    WR_REAL S = WR_REAL(0.0);
    for (int Band = 0; Band < WR_DETAIL_BANDS; ++Band)
    {
        const WR_REAL Fade = WR_saturate(WR_REAL(1.0) - Footprint * WR_DETAIL_FREQUENCY[Band]);
        if (Fade > WR_REAL(0.0))
        {
            S += Fade * WR_DetailHeight(DX, DY, DZ, Band, OX, OY, OZ);
        }
    }
    return PeakR * WR_PeakCap(S / WR_S_MAX);
}

// The share of a disc above a straight horizon X of its radii above the
// disc's centre: 1 at X = -1, 0.5 at 0, 0 at 1.
WR_REAL WR_DiscAbove(WR_REAL X)
{
    const WR_REAL C = WR_min(WR_REAL(1.0), WR_max(WR_REAL(-1.0), X));
    return (WR_acos(C) - C * WR_sqrt(WR_max(WR_REAL(0.0), WR_REAL(1.0) - C * C))) / WR_PI;
}

struct WR_Shadow
{
    WR_REAL Visible;   // the star's disc seen past the ground, 0..1
    WR_REAL Horizon;   // the highest horizon the samples found, rad above level; -pi/2 when none
    WR_REAL Bands;     // how many band evaluations it took: the cost the budget pays
};

// D the unit direction to the point in the body's axes; L the unit direction
// to the star in the same axes; Footprint the pixel's, radius units;
// ReliefScale FWorldRelief::SlopeScale (PeakCm / (R x S_max)), 0 where there
// is no ground; SunRadius the star's angular radius, rad; Samples how many
// samples the march takes (SkyMaterial::ShadowSamples).
WR_Shadow WR_SunVisible(WR_REAL DX, WR_REAL DY, WR_REAL DZ, WR_REAL LX, WR_REAL LY, WR_REAL LZ, WR_REAL Footprint,
                        WR_REAL OX, WR_REAL OY, WR_REAL OZ, WR_REAL ReliefScale, WR_REAL SunRadius, WR_REAL Samples)
{
    WR_Shadow Out;
    Out.Visible = WR_REAL(1.0);
    Out.Horizon = -WR_PI / WR_REAL(2.0);
    Out.Bands = WR_REAL(0.0);
    const WR_REAL PeakR = ReliefScale * WR_S_MAX;
    if (!(PeakR > WR_REAL(0.0)))
    {
        return Out;
    }
    // The sun's elevation above the level ground, and its azimuth along it.
    const WR_REAL SinT = LX * DX + LY * DY + LZ * DZ;
    const WR_REAL TX = LX - SinT * DX;
    const WR_REAL TY = LY - SinT * DY;
    const WR_REAL TZ = LZ - SinT * DZ;
    const WR_REAL CosT = WR_sqrt(TX * TX + TY * TY + TZ * TZ);
    const WR_REAL Theta = WR_atan(SinT / WR_max(CosT, WR_REAL(1.0e-6)));
    // No horizon the march can find is steeper than the steepest ground, nor
    // lower than its negative: above the one the disc is whole, below the
    // other it is hidden (and no slope faces it: the lambert is 0 there).
    const WR_REAL Steepest = WR_atan(ReliefScale * WR_SHADOW_GRADIENT_MAX);
    if (Theta - SunRadius >= Steepest || CosT < WR_REAL(1.0e-6))
    {
        return Out;
    }
    if (Theta + SunRadius <= -Steepest)
    {
        Out.Visible = WR_REAL(0.0);
        return Out;
    }
    const WR_REAL UX = TX / CosT;
    const WR_REAL UY = TY / CosT;
    const WR_REAL UZ = TZ / CosT;
    // The pixel's own height, every band its footprint keeps.
    WR_REAL S = WR_REAL(0.0);
    for (int Band = 0; Band < WR_DETAIL_BANDS; ++Band)
    {
        const WR_REAL Fade = WR_saturate(WR_REAL(1.0) - Footprint * WR_DETAIL_FREQUENCY[Band]);
        if (Fade > WR_REAL(0.0))
        {
            S += Fade * WR_DetailHeight(DX, DY, DZ, Band, OX, OY, OZ);
            Out.Bands += WR_REAL(1.0);
        }
    }
    const WR_REAL Here = PeakR * WR_PeakCap(S / WR_S_MAX);
    // The end: where the sun's lower edge, rising at tan(theta - r) with the
    // ground curving away as s^2 / 2, clears the highest peak.
    const WR_REAL Lower = WR_tan(Theta - SunRadius);
    const WR_REAL Rise = WR_max(WR_REAL(0.0), PeakR - Here);
    const WR_REAL End = WR_sqrt(Lower * Lower + WR_REAL(2.0) * Rise) - Lower;
    const WR_REAL Nearest = WR_max(Footprint, WR_REAL(0.5) / WR_DETAIL_FREQUENCY[WR_DETAIL_BANDS - 1]);
    if (!(End > Nearest))
    {
        return Out;
    }
    const int Count = int(WR_max(Samples, WR_REAL(2.0)) + WR_REAL(0.5));
    const WR_REAL Growth = WR_pow(End / Nearest, WR_REAL(1.0) / WR_REAL(Count - 1));
    const WR_REAL Hidden = WR_tan(Theta + SunRadius);
    WR_REAL Along = Nearest;
    WR_REAL Before = WR_REAL(0.0);
    WR_REAL Highest = WR_REAL(-1.0e9);
    WR_LOOP for (int Sample = 0; Sample < Count; ++Sample)
    {
        // Two exact exits: the whole disc already hidden, or no ground from
        // here on -- none above PeakCm, all of it curving away -- able to
        // rise above the highest horizon found.
        if (Highest >= Hidden || (PeakR - Here - WR_REAL(0.5) * Along * Along) / Along <= Highest)
        {
            break;
        }
        const WR_REAL Seen = WR_max(Footprint, WR_SHADOW_FOOTPRINT_FACTOR * (Along - Before));
        const WR_REAL PX = DX + UX * Along;
        const WR_REAL PY = DY + UY * Along;
        const WR_REAL PZ = DZ + UZ * Along;
        const WR_REAL PL = WR_sqrt(PX * PX + PY * PY + PZ * PZ);
        WR_REAL There = WR_REAL(0.0);
        for (int Band = 0; Band < WR_DETAIL_BANDS; ++Band)
        {
            const WR_REAL Fade = WR_saturate(WR_REAL(1.0) - Seen * WR_DETAIL_FREQUENCY[Band]);
            if (Fade > WR_REAL(0.0))
            {
                There += Fade * WR_DetailHeight(PX / PL, PY / PL, PZ / PL, Band, OX, OY, OZ);
                Out.Bands += WR_REAL(1.0);
            }
        }
        const WR_REAL Above = PeakR * WR_PeakCap(There / WR_S_MAX) - Here - WR_REAL(0.5) * Along * Along;
        Highest = WR_max(Highest, Above / Along);
        Before = Along;
        Along *= Growth;
    }
    Out.Horizon = WR_atan(Highest);
    Out.Visible = SunRadius > WR_REAL(0.0) ? WR_DiscAbove((Out.Horizon - Theta) / SunRadius)
                                          : WR_step(Out.Horizon, Theta);
    return Out;
}
```

- [ ] **Step 5: The C++ mirror.** In `Source/DeepSpace/Surface/WorldRelief.h`, add this after `struct FFaceTerms`:

```cpp
/** How much of its star a point of the ground sees past the ground: the
 *  cast shadow, WR_SunVisible (the developer's ruling on slice (b)'s
 *  build). Visible 0..1; the highest horizon the march found, rad above the
 *  level (-pi/2 where it found none or never marched); and how many band
 *  evaluations it took -- the cost the GPU pays per pixel. */
struct FSunVisibility
{
    double Visible = 1.0;
    double Horizon = -UE_DOUBLE_HALF_PI;
    int32 Bands = 0;
};
```

In `namespace WorldReliefNoise`, before `struct FBands`, add:

```cpp
    /** The simplex's value alone, as the march reads it (WR_SimplexValueAt),
     *  in double and in float; and WR_SimplexAt's value in float, which it
     *  must equal. */
    DEEPSPACE_API double SimplexValue(const FVector3d& V);
    DEEPSPACE_API float SimplexValueF32(const FVector3f& V);
    DEEPSPACE_API float SimplexF32(const FVector3f& V);

    /** WR_PeakCap: FWorldRelief::PeakCap's value, from the shared file. */
    DEEPSPACE_API double PeakCap(double X);

    /** WR_ShadowHeight: the height the march reads, radius units above the
     *  datum -- the twelve detail bands at a footprint (D units), no
     *  craters -- for PeakR = PeakCm / R. */
    DEEPSPACE_API double ShadowHeight(const FVector3d& D, double FootprintD, const FVector3d& Offset, double PeakR);

    /** WR_DiscAbove: the share of a disc above a straight horizon X of its
     *  radii above its centre. */
    DEEPSPACE_API double DiscAbove(double X);

    /** WR_SunVisible in double, and in float -- the GPU's operations in the
     *  GPU's precision, the parity test's floor. L is the unit direction to
     *  the star in the body's axes, ReliefScale FWorldRelief::SlopeScale. */
    DEEPSPACE_API FSunVisibility SunVisibleF64(const FVector3d& D, const FVector3d& L, double FootprintD, const FVector3d& Offset,
                                               double ReliefScale, double SunRadius, int32 Samples);
    DEEPSPACE_API FSunVisibility SunVisibleF32(const FVector3f& D, const FVector3f& L, float FootprintD, const FVector3f& Offset,
                                               float ReliefScale, float SunRadius, int32 Samples);

    /** The march's constants, as the shared file has them. */
    struct FShadowConstants
    {
        double SMax = 0.0;
        double PeakCapKnee = 0.0;
        double GradientMax = 0.0;
        double FootprintFactor = 0.0;
    };
    DEEPSPACE_API FShadowConstants ShadowConstants();
```

In `namespace WorldReliefShading`, add:

```cpp
    /** Both materials' cast shadow at D: WR_SunVisible for the light L (unit,
     *  body axes), the star's angular radius (rad), the pixel's footprint
     *  (radius units) and the march's sample count, at FWorldRelief's
     *  SlopeScale -- 0, no shadow, on a world with no ground. */
    DEEPSPACE_API FSunVisibility SunVisible(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& L,
                                            double SunRadius, double FootprintRadius, int32 Samples);
```

In `Source/DeepSpace/Surface/WorldRelief.cpp`, add to `namespace WorldReliefLocal`:

```cpp
    template <typename TShadow>
    FSunVisibility ToSunVisibility(const TShadow& Shadow)
    {
        FSunVisibility Out;
        Out.Visible = Shadow.Visible;
        Out.Horizon = Shadow.Horizon;
        Out.Bands = FMath::RoundToInt32(static_cast<double>(Shadow.Bands));
        return Out;
    }
```

After `WorldReliefNoise::FaceF32`, add:

```cpp
double WorldReliefNoise::SimplexValue(const FVector3d& V)
{
    return WorldReliefF64::WR_SimplexValueAt(V.X, V.Y, V.Z, 0, 0, 0);
}

float WorldReliefNoise::SimplexValueF32(const FVector3f& V)
{
    return WorldReliefF32::WR_SimplexValueAt(V.X, V.Y, V.Z, 0, 0, 0);
}

float WorldReliefNoise::SimplexF32(const FVector3f& V)
{
    return WorldReliefF32::WR_Simplex(V.X, V.Y, V.Z).Value;
}

double WorldReliefNoise::PeakCap(double X)
{
    return WorldReliefF64::WR_PeakCap(X);
}

double WorldReliefNoise::ShadowHeight(const FVector3d& D, double FootprintD, const FVector3d& Offset, double PeakR)
{
    return WorldReliefF64::WR_ShadowHeight(D.X, D.Y, D.Z, FootprintD, Offset.X, Offset.Y, Offset.Z, PeakR);
}

double WorldReliefNoise::DiscAbove(double X)
{
    return WorldReliefF64::WR_DiscAbove(X);
}

FSunVisibility WorldReliefNoise::SunVisibleF64(const FVector3d& D, const FVector3d& L, double FootprintD, const FVector3d& Offset,
                                               double ReliefScale, double SunRadius, int32 Samples)
{
    return WorldReliefLocal::ToSunVisibility(WorldReliefF64::WR_SunVisible(D.X, D.Y, D.Z, L.X, L.Y, L.Z, FootprintD,
        Offset.X, Offset.Y, Offset.Z, ReliefScale, SunRadius, static_cast<double>(Samples)));
}

FSunVisibility WorldReliefNoise::SunVisibleF32(const FVector3f& D, const FVector3f& L, float FootprintD, const FVector3f& Offset,
                                               float ReliefScale, float SunRadius, int32 Samples)
{
    return WorldReliefLocal::ToSunVisibility(WorldReliefF32::WR_SunVisible(D.X, D.Y, D.Z, L.X, L.Y, L.Z, FootprintD,
        Offset.X, Offset.Y, Offset.Z, ReliefScale, SunRadius, static_cast<float>(Samples)));
}

WorldReliefNoise::FShadowConstants WorldReliefNoise::ShadowConstants()
{
    FShadowConstants Out;
    Out.SMax = WorldReliefF64::WR_S_MAX;
    Out.PeakCapKnee = WorldReliefF64::WR_PEAK_CAP_KNEE;
    Out.GradientMax = WorldReliefF64::WR_SHADOW_GRADIENT_MAX;
    Out.FootprintFactor = WorldReliefF64::WR_SHADOW_FOOTPRINT_FACTOR;
    return Out;
}
```

At the end of the file, add:

```cpp
FSunVisibility WorldReliefShading::SunVisible(const FWorldReliefParams& Params, const FVector3d& D, const FVector3d& L,
                                              double SunRadius, double FootprintRadius, int32 Samples)
{
    return WorldReliefNoise::SunVisibleF64(D, L, FootprintRadius, Params.SeedOffset, FWorldRelief(Params).SlopeScale(), SunRadius, Samples);
}
```

- [ ] **Step 6: Run them to see them pass.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && ./test.sh DeepSpace.Surface.WorldRelief && ./test.sh DeepSpace.Surface.ShaderMapping`

Expected: every `DeepSpace.Surface.WorldRelief.*` passes, the five new ones included, and so do `.KnownValues` and `.OffsetSplit`: the file's old functions are untouched. `.ShadowAgainstTruth`'s info lines print ratios near planning's 0.79-0.91. If `.ShadowGradientMax` reports a measured value outside 12.5-15.2, the constant's margin claim is off. Stop and report the measured value, and do not re-tune without the rule.

- [ ] **Step 7: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Shaders/Private/WorldRelief.ush Source/DeepSpace/Surface/WorldRelief.h \
  Source/DeepSpace/Surface/WorldRelief.cpp Source/DeepSpace/Tests/WorldReliefShadowTest.cpp && \
git commit -qm "feat(surface): WR_SunVisible -- the cast shadow marched through the height function toward the sun, and its C++ mirror

Twelve detail bands, no craters; geometric samples from the pixel to where
the sun's lower edge clears PeakCm over the curving ground; each sample at
twice its spacing, so it never aliases; the star's own disc for softness;
exact exits past the steepest ground. DeepSpace.Surface.WorldRelief.
ShadowHeight, .ShadowGradientMax, .SunDisc, .ShadowAgainstTruth, .ShadowCost.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 8: Prove them.** Each mutation is run after the commit, followed by `./build.sh`:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'WR_SHADOW_FOOTPRINT_FACTOR = WR_REAL(2.0);' 'WR_SHADOW_FOOTPRINT_FACTOR = WR_REAL(0.0);' 'DeepSpace.Surface.WorldRelief.ShadowCost$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'Out.Visible = SunRadius > WR_REAL(0.0) ?' 'Out.Visible = SunRadius < WR_REAL(0.0) ?' 'DeepSpace.Surface.WorldRelief.SunDisc$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'if (Theta - SunRadius >= Steepest ||' 'if (Theta >= Steepest ||' 'DeepSpace.Surface.WorldRelief.SunDisc$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'return (WR_acos(C) - C * WR_sqrt' 'return (WR_acos(C) + C * WR_sqrt' 'DeepSpace.Surface.WorldRelief.SunDisc$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'return X < WR_REAL(0.0) ? -Capped : Capped;' 'return X < WR_REAL(0.0) ? -Magnitude : Magnitude;' 'DeepSpace.Surface.WorldRelief.ShadowHeight$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush '    return Smooth * GF;
}

// WR_SimplexAt' '    return Smooth * GF * WR_REAL(1.0001);
}

// WR_SimplexAt' 'DeepSpace.Surface.WorldRelief.ShadowHeight$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'WR_SHADOW_GRADIENT_MAX = WR_REAL(21.3);' 'WR_SHADOW_GRADIENT_MAX = WR_REAL(14.0);' 'DeepSpace.Surface.WorldRelief.ShadowGradientMax$'; \
Tools/mutate.sh Shaders/Private/WorldRelief.ush 'const WR_REAL PX = DX + UX * Along;' 'const WR_REAL PX = DX - UX * Along;' 'DeepSpace.Surface.WorldRelief.ShadowAgainstTruth$'; \
./build.sh
```

Expected: eight `KILLED`.
- The factor-0 mutant reads about 187 bands, against the 9.2-a-sample cap.
- The step-softness mutant gives 1 on the level, not 0.5.
- The exit without `r` skips the march where it must run.
- The `+` disc breaks the symmetry.
- The uncapped height misses `FWorldRelief::PeakCap` past the knee.
- The scaled corner breaks the value's equality.
- The 14.0 gradient falls under 1.4 x the measured maximum.
- The march that turns away from the sun in X leaves the truth.

The multi-line mutant needs `mutate.sh`'s exact-text match. If it reports "text not found", use the unique single line `    return Smooth * GF;` together with the `WR_SimplexCornerValue` definition's first line as context, as `mutate.sh`'s usage allows.

A mutant that drops the curvature from the sample's `Above` (`- WR_REAL(0.5) * Along * Along`) was measured while planning to be equivalent at these scales. The shadowed share changed by at most 0.3 points: the drop is at most 0.8 degrees at the march's far end, where the horizon is rarely found. It is not claimed as proven, and it stays in because it is the geometry.

---

## Task 3: the materials, the contract, and the sky's parameters

**Owner:** T. **Depends on:** Task 2.

**Files:**
- Modify: `Tools/sky_material_contract.json`. Add the parameters `sun_radius`, `shadows` and `probe_light`. Add `sun_radius` and `shadows` to `M_SkyBody`'s and `M_SkyGround`'s lists. Add a new material `M_SkyShadowProbe`, a new block `shadow` and a new constant `shadow_samples`.
- Modify: `Source/DeepSpace/Sky/SkyMaterialContract.h`. Add `SunRadius`, `Shadows`, `ProbeLight`, `ShadowProbePath`, `ShadowEntry`, `ShadowInputs()`, `ShadowStrengthPin`, `ShadowSamples`, `ShadowProbeScalars()` and `ShadowProbeVectors()`, and extend `BodyScalars()` and `GroundScalars()`.
- Modify: `Tools/setup_sky_materials.py`. Add `SHADOW`, `SHADOW_CODE`, `SHADOW_PROBE_CODE`, `DEFAULT_SUN_RADIUS`, `cast_shadow()` and `shadow_probe()`; wire the shadow into `sky_body()` and `sky_ground()`; call it from `main()`.
- Modify: `Source/DeepSpace/Sky/ShipSky.h` and `.cpp`. Add `ShipSky::SunAngularRadius` and `ShipSky::ShadowStrength`, and the CVar `ds.Sky.Shadows`. `DrawBodies` writes both parameters, and `CopyBodyLook` copies them.
- Modify: `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp`. Add roles, `M_SkyShadowProbe`, and a `CheckSharedRelief` that finds its node by the entry. Add `CheckShadow`, and the shadow block and constant in `CheckSharedTables`.
- Create: `Source/DeepSpace/Tests/ShipSkyShadowTest.cpp` (`DeepSpace.Sky.ShadowParameters`)

**Interfaces:**
- Consumes: `WR_SunVisible` (Task 2); `setup_sky_materials.py`'s `custom_node`, `seed_offset`, `to_body`, `probe_direction` and `mask`; `ShipSky::CopyBodyLook`.
- Produces:

```cpp
namespace SkyMaterial {
    inline const FName SunRadius, Shadows, ProbeLight, ShadowStrengthPin;
    inline const TCHAR* const ShadowProbePath, ShadowEntry;   // "WR_SunVisible"
    inline TArray<FName> ShadowInputs();   // D, L, Footprint, SeedOffset, ReliefScale, SunRadius, Samples
    inline constexpr int32 ShadowSamples = 12;
    inline TArray<FName> ShadowProbeScalars(), ShadowProbeVectors();
}
namespace ShipSky {
    DEEPSPACE_API double SunAngularRadius(const FSkySystem& System, int32 Body);
    DEEPSPACE_API float ShadowStrength();
}
```

  It also produces the CVar `ds.Sky.Shadows` (float, default 1).

- [ ] **Step 1: Write the failing tests.**

Create `Source/DeepSpace/Tests/ShipSkyShadowTest.cpp`:

```cpp
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"
#include "Sky/LocalSystem.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldGround.h"
#include "Tests/SkyTestFixtures.h"
#include "Tests/SkyTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipSkyShadowParametersTest, "DeepSpace.Sky.ShadowParameters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipSkyShadowTestLocal
{
    float ScalarOf(UPrimitiveComponent* Component, FName Name)
    {
        UMaterialInstanceDynamic* Instance = Component ? Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0)) : nullptr;
        return Instance ? Instance->K2_GetScalarParameterValue(Name) : -1.0f;
    }
}

/**
 * The cast shadow's two parameters as the sky writes them: SunRadius, the
 * star's angular radius from each world, and Shadows, ds.Sky.Shadows
 * clamped, on every world's proxy and copied into the ground's look.
 */
bool FShipSkyShadowParametersTest::RunTest(const FString& Parameters)
{
    using namespace ShipSkyShadowTestLocal;

    // -- The star's angular radius, pure --------------------------------------
    const FSkySystem Fixture = SkyTestFixtures::System();
    const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
    const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
    const double Expected = FMath::Asin(Star.Radius / (Star.Position - Home.Position).Size());
    TestTrue(FString::Printf(TEXT("a world sees its star asin(R / d) in radius (%.6f against %.6f)"),
        ShipSky::SunAngularRadius(Fixture, SkyTestFixtures::HomeIndex), Expected),
        FMath::IsNearlyEqual(ShipSky::SunAngularRadius(Fixture, SkyTestFixtures::HomeIndex), Expected, 1e-12));
    TestEqual(TEXT("a star has no sun"), ShipSky::SunAngularRadius(Fixture, SkyTestFixtures::StarIndex), 0.0);
    TestEqual(TEXT("nor has a body that is not there"), ShipSky::SunAngularRadius(Fixture, 99), 0.0);
    FSkySystem Inside = Fixture;
    Inside.Bodies[SkyTestFixtures::HomeIndex].Position = Star.Position + FVector(0.5 * Star.Radius, 0.0, 0.0);
    TestEqual(TEXT("from inside its star's radius the star fills half the sky, finite"),
        ShipSky::SunAngularRadius(Inside, SkyTestFixtures::HomeIndex), UE_DOUBLE_HALF_PI);

    // -- ds.Sky.Shadows, clamped ----------------------------------------------
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    if (!TestNotNull(TEXT("ds.Sky.Shadows exists"), Shadows))
    {
        return false;
    }
    const float Was = Shadows->GetFloat();
    for (const TPair<float, float>& Case : TArray<TPair<float, float>>{ { 7.0f, 1.0f }, { -1.0f, 0.0f }, { 0.5f, 0.5f }, { 1.0f, 1.0f } })
    {
        Shadows->Set(Case.Key, ECVF_SetByCode);
        TestEqual(FString::Printf(TEXT("ds.Sky.Shadows %.1f draws %.1f"), Case.Key, Case.Value), ShipSky::ShadowStrength(), Case.Value);
    }

    // -- Live: every world's proxy, and the ground's copy ---------------------
    {
        SkyTestWorld::FSkyWorld Test(TEXT("ShadowParametersWorld"));
        Test.BeginPlay();
        // 0.5: a parameter the material lacks reads back 0, so 0 would pass by accident.
        Shadows->Set(0.5f, ECVF_SetByCode);
        Test.Step(1.0f / 60.0f);
        const FSkySystem Here = LocalSystem::Here(Test.World);
        for (int32 Index = 0; Index < Here.Bodies.Num(); ++Index)
        {
            if (Here.Bodies[Index].Kind == ESkyBodyKind::Star)
            {
                continue;
            }
            UStaticMeshComponent* Proxy = Test.Sky->GetProxy(Index);
            const float Want = static_cast<float>(ShipSky::SunAngularRadius(Here, Index));
            TestTrue(FString::Printf(TEXT("%s's proxy carries its star's radius (%.6f against %.6f)"), *Here.Bodies[Index].Id.ToString(),
                ScalarOf(Proxy, SkyMaterial::SunRadius), Want), Want > 0.0f && FMath::IsNearlyEqual(ScalarOf(Proxy, SkyMaterial::SunRadius), Want, 1e-6f * Want));
            TestEqual(FString::Printf(TEXT("%s's proxy carries the shadows' strength"), *Here.Bodies[Index].Id.ToString()), ScalarOf(Proxy, SkyMaterial::Shadows), 0.5f);
        }
        // Below 50 km over Baemsekai IV the ground draws the body, in its look.
        const FSkyBody& Fourth = Here.Bodies[4];
        const FVector Up = (Test.Ship->GetFlightState().GetUniversePosition() - Fourth.Position).GetSafeNormal();
        Test.Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + 2.0e6), FRotationMatrix::MakeFromX(-Up).ToQuat());
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        UMaterialInstanceDynamic* Look = Test.Ground->GetGroundMaterialInstance();
        if (TestTrue(TEXT("the ground has Baemsekai IV"), Test.Ground->IsDrawingBody() && Look))
        {
            TestEqual(TEXT("and its star's radius"), Look->K2_GetScalarParameterValue(SkyMaterial::SunRadius),
                ScalarOf(Test.Sky->GetProxy(4), SkyMaterial::SunRadius));
            TestEqual(TEXT("and the shadows' strength"), Look->K2_GetScalarParameterValue(SkyMaterial::Shadows), 0.5f);
        }
    }
    Shadows->Set(Was, ECVF_SetByCode);
    return true;
}

#endif
```

In `Source/DeepSpace/Tests/SkyMaterialContractTest.cpp`, make the following changes. First, add the include `#include "Materials/MaterialExpressionTransform.h"`. Then add three roles to `Roles()`:

```cpp
            { TEXT("sun_radius"), SkyMaterial::SunRadius, TEXT("scalar") },
            { TEXT("shadows"), SkyMaterial::Shadows, TEXT("scalar") },
            { TEXT("probe_light"), SkyMaterial::ProbeLight, TEXT("vector") },
```

In `CheckSharedRelief`, count only the node that calls the entry, since `M_SkyBody` and `M_SkyGround` now hold more Custom nodes. Replace the collecting `if` and the count check:

```cpp
            if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression.Get()))
            {
                if (Custom->Code.Contains(FString(SkyMaterial::WorldReliefEntry) + TEXT("(")))
                {
                    Customs.Add(Custom);
                }
            }
```

```cpp
        if (!Test.TestEqual(FString::Printf(TEXT("%s calls the shared file's %s from one Custom node"), *Asset, SkyMaterial::WorldReliefEntry),
                            Customs.Num(), 1))
```

After `CheckSharedRelief`, add:

```cpp
    enum class EShadowHost : uint8 { Body, Ground, Probe };

    bool HasParameter(const TSet<const UMaterialExpression*>& Nodes, FName Name)
    {
        for (const UMaterialExpression* Node : Nodes)
        {
            const UMaterialExpressionParameter* Parameter = Cast<UMaterialExpressionParameter>(Node);
            if (Parameter && Parameter->ParameterName == Name)
            {
                return true;
            }
        }
        return false;
    }

    /** The cast shadow as a material reaches it (the developer's ruling on
     *  slice (b)'s build): one Custom node that includes WorldRelief.ush and
     *  calls WR_SunVisible, its pins the header's, fed what the face is fed
     *  -- its D the shared terms' own Direction, its seed the world's -- with
     *  the world's light turned into the body's axes on the proxy and into
     *  the tile's on the ground, the star's radius, the contract's sample
     *  count, and on the proxy a ReliefScale that Banding takes out (a
     *  giant's billow is not ground). The emissive reads it. */
    void CheckShadow(FAutomationTestBase& Test, const UMaterial& Material, const UMaterialExpressionCustom* Shared, EShadowHost Host)
    {
        const FString Asset = Material.GetName();
        TArray<const UMaterialExpressionCustom*> Calls;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression.Get());
            if (Custom && Custom->Code.Contains(FString(SkyMaterial::ShadowEntry) + TEXT("(")))
            {
                Calls.Add(Custom);
            }
        }
        if (!Test.TestEqual(FString::Printf(TEXT("%s calls %s from one Custom node"), *Asset, SkyMaterial::ShadowEntry), Calls.Num(), 1))
        {
            return;
        }
        const UMaterialExpressionCustom* Node = Calls[0];
        Test.TestTrue(FString::Printf(TEXT("%s's shadow includes %s, and only it"), *Asset, SkyMaterial::WorldReliefInclude),
            Node->IncludeFilePaths.Num() == 1 && Node->IncludeFilePaths[0] == SkyMaterial::WorldReliefInclude);
        TArray<FName> Want = SkyMaterial::ShadowInputs();
        if (Host != EShadowHost::Probe)
        {
            Want.Add(SkyMaterial::ShadowStrengthPin);
        }
        TArray<FName> Pins;
        for (const FCustomInput& Input : Node->Inputs)
        {
            Pins.Add(Input.InputName);
            Test.TestNotNull(FString::Printf(TEXT("%s's shadow's %s is wired"), *Asset, *Input.InputName.ToString()), Input.Input.Expression);
        }
        Test.TestTrue(FString::Printf(TEXT("%s's shadow's pins are the header's, in order"), *Asset), Pins == Want);
        const auto Pin = [Node](FName Name) -> const UMaterialExpression*
        {
            for (const FCustomInput& Input : Node->Inputs)
            {
                if (Input.InputName == Name)
                {
                    return Input.Input.Expression;
                }
            }
            return nullptr;
        };
        if (Shared)
        {
            const UMaterialExpression* Direction = nullptr;
            for (const FCustomInput& Input : Shared->Inputs)
            {
                Direction = Input.InputName == FName(TEXT("Direction")) ? Input.Input.Expression.Get() : Direction;
            }
            Test.TestTrue(FString::Printf(TEXT("%s's shadow is marched from the face's own D"), *Asset), Direction && Pin(TEXT("D")) == Direction);
        }
        Test.TestTrue(FString::Printf(TEXT("%s's shadow reads the world's seed"), *Asset), HasParameter(Upstream(Pin(TEXT("SeedOffset"))), SkyMaterial::SurfaceSeed));
        Test.TestTrue(FString::Printf(TEXT("%s's shadow reads its star's radius"), *Asset), HasParameter(Upstream(Pin(TEXT("SunRadius"))), SkyMaterial::SunRadius));
        Test.TestTrue(FString::Printf(TEXT("%s's shadow reads the ground's slope scale"), *Asset), HasParameter(Upstream(Pin(TEXT("ReliefScale"))), SkyMaterial::ReliefScale));
        const UMaterialExpressionConstant* Samples = Cast<UMaterialExpressionConstant>(Pin(TEXT("Samples")));
        Test.TestTrue(FString::Printf(TEXT("%s's shadow takes the contract's %d samples"), *Asset, SkyMaterial::ShadowSamples),
            Samples && FMath::IsNearlyEqual(Samples->R, static_cast<float>(SkyMaterial::ShadowSamples)));
        if (Host == EShadowHost::Probe)
        {
            Test.TestTrue(TEXT("the probe's light is ProbeLight, in body axes"), HasParameter(Upstream(Pin(TEXT("L"))), SkyMaterial::ProbeLight));
            return;
        }
        Test.TestTrue(FString::Printf(TEXT("%s's shadow is drawn as Shadows says"), *Asset), HasParameter(Upstream(Pin(TEXT("Strength"))), SkyMaterial::Shadows));
        const TSet<const UMaterialExpression*> Light = Upstream(Pin(TEXT("L")));
        Test.TestTrue(FString::Printf(TEXT("%s's shadow is toward the world's light"), *Asset), HasParameter(Light, SkyMaterial::LightDirection));
        if (Host == EShadowHost::Body)
        {
            Test.TestTrue(TEXT("M_SkyBody turns the light into the body's axes for it"), HasParameter(Light, SkyMaterial::BodyAxisX));
            Test.TestTrue(TEXT("and takes a giant's billow out of its ReliefScale by Banding"),
                HasParameter(Upstream(Pin(TEXT("ReliefScale"))), SkyMaterial::Banding));
        }
        else
        {
            bool bToLocal = false;
            for (const UMaterialExpression* Found : Light)
            {
                const UMaterialExpressionTransform* Transform = Cast<UMaterialExpressionTransform>(Found);
                bToLocal |= Transform && Transform->TransformSourceType == TRANSFORMSOURCE_World && Transform->TransformType == TRANSFORM_Local;
            }
            Test.TestTrue(TEXT("M_SkyGround turns the light into the tile's axes, the universe's, for it"), bToLocal);
        }
        const FExpressionInput* Emissive = Material.GetExpressionInputForProperty(MP_EmissiveColor);
        Test.TestTrue(FString::Printf(TEXT("%s's emissive reads the shadow"), *Asset), Emissive && Upstream(Emissive->Expression).Contains(Node));
    }
```

In `CheckSurfaceFace`, right after `const UMaterialExpressionCustom* Shared = CheckSharedRelief(Test, Material); if (!Shared) { return; }`, add:

```cpp
        CheckShadow(Test, Material, Shared, EShadowHost::Body);
```

In `CheckSharedTables`, at its end, add:

```cpp
        const TSharedPtr<FJsonObject> Shadow = Contract->GetObjectField(TEXT("shadow"));
        Test.TestEqual(TEXT("the JSON's shadow entry is the header's"), Shadow->GetStringField(TEXT("entry")), FString(SkyMaterial::ShadowEntry));
        TArray<FName> ShadowPins;
        for (const TSharedPtr<FJsonValue>& Value : Shadow->GetArrayField(TEXT("inputs")))
        {
            ShadowPins.Add(FName(*Value->AsString()));
        }
        Test.TestTrue(TEXT("and its pins"), ShadowPins == SkyMaterial::ShadowInputs());
        const TSharedPtr<FJsonObject> Constants = Contract->GetObjectField(TEXT("constants"));
        Test.TestEqual(TEXT("the march's sample count is the header's"),
            static_cast<int32>(Constants->GetNumberField(TEXT("shadow_samples"))), SkyMaterial::ShadowSamples);
        Test.TestEqual(TEXT("and a sample's footprint is filter_pixels times its spacing, the face's own rule"),
            WorldReliefNoise::ShadowConstants().FootprintFactor, Constants->GetNumberField(TEXT("filter_pixels")));
```

In `RunTest`, add to `Materials` the new entry:

```cpp
        { TEXT("M_SkyShadowProbe"), SkyMaterial::ShadowProbePath, SkyMaterial::ShadowProbeScalars(), SkyMaterial::ShadowProbeVectors() },
```

Beside the `M_SkyReliefProbe` branch in the loop, add:

```cpp
        if (Material->GetFName() == TEXT("M_SkyGround"))
        {
            CheckShadow(*this, *Material, CheckSharedRelief(*this, *Material), EShadowHost::Ground);
        }
        if (Material->GetFName() == TEXT("M_SkyShadowProbe"))
        {
            CheckShadow(*this, *Material, nullptr, EShadowHost::Probe);
        }
```

- [ ] **Step 2: Run them to see them fail.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh`

Expected: the build fails. `SkyMaterial::SunRadius`, `ShipSky::SunAngularRadius` and the rest are undeclared.

- [ ] **Step 3: The header's side of the contract.** In `Source/DeepSpace/Sky/SkyMaterialContract.h`, after `SurfaceSeed`'s line, add:

```cpp
    inline const FName SunRadius = TEXT("SunRadius");           // scalar: the star's angular radius from the body, rad; ShipSky::SunAngularRadius
    inline const FName Shadows = TEXT("Shadows");               // scalar: 0..1, how much of the cast shadow is drawn; ds.Sky.Shadows
```

After the probe's names (`ProbeBias`), add:

```cpp
    inline const FName ProbeLight = TEXT("ProbeLight");         // vector, the shadow probe's: the light in body axes
    inline const TCHAR* const ShadowProbePath = TEXT("/Game/Materials/Sky/M_SkyShadowProbe.M_SkyShadowProbe");
```

After `WorldReliefOutputs()`, add:

```cpp
    // The cast shadow (the developer's ruling on slice (b)'s build): a second
    // Custom node over the same file, in M_SkyBody and M_SkyGround.
    inline const TCHAR* const ShadowEntry = TEXT("WR_SunVisible");
    inline TArray<FName> ShadowInputs() { return { TEXT("D"), TEXT("L"), TEXT("Footprint"), TEXT("SeedOffset"), TEXT("ReliefScale"), TEXT("SunRadius"), TEXT("Samples") }; }
    /** The materials' node takes one more pin, Shadows' strength: 0 skips the march. */
    inline const FName ShadowStrengthPin = TEXT("Strength");
    /** How many samples the march takes: the JSON's shadow_samples, which the
     *  materials are authored with. Task 4's budget gate chose it. */
    inline constexpr int32 ShadowSamples = 12;
```

Replace `BodyScalars()` and `GroundScalars()`, and add the probe's lists:

```cpp
    inline TArray<FName> BodyScalars() { return { Brightness, PointBlend, Mottle, Detail, Banding, ReliefScale, Cratering, SunRadius, Shadows }; }
    inline TArray<FName> GroundScalars() { return { Brightness, Mottle, Detail, Cratering, ReliefScale, Morph, BandLimit, SunRadius, Shadows }; }
    inline TArray<FName> ShadowProbeScalars() { return { ReliefScale, SunRadius, ProbeFootprint }; }
    inline TArray<FName> ShadowProbeVectors() { return { SurfaceSeed, ProbeLight, ProbeBias }; }
```

- [ ] **Step 4: The JSON's side.** In `Tools/sky_material_contract.json`:
  - `"parameters"` gains `"sun_radius": {"name": "SunRadius", "type": "scalar"}`, `"shadows": {"name": "Shadows", "type": "scalar"}` and `"probe_light": {"name": "ProbeLight", "type": "vector"}`.
  - `"materials"`: `M_SkyBody`'s and `M_SkyGround`'s lists gain `"sun_radius", "shadows"`, and there is a new entry `"M_SkyShadowProbe": {"parameters": ["surface_seed", "relief_scale", "sun_radius", "probe_footprint", "probe_light", "probe_bias"]}`.
  - A new top-level block, after `"shared_relief"`:

```json
  "shadow": {
    "comment": "The cast shadow (the developer's ruling on slice (b)'s build, 2026-09-28): a second Custom node over the same file, in M_SkyBody and M_SkyGround, calling WR_SunVisible. The materials' node adds one pin, Strength (Shadows), which skips the march at 0. SkyMaterialContract.h holds the same entry and pins; DeepSpace.Sky.MaterialContract holds the built nodes to them.",
    "entry": "WR_SunVisible",
    "inputs": ["D", "L", "Footprint", "SeedOffset", "ReliefScale", "SunRadius", "Samples"]
  },
```

  - `"constants"` gains `"shadow_samples": 12`.

- [ ] **Step 5: The sky writes them.** In `Source/DeepSpace/Sky/ShipSky.h`, in `namespace ShipSky` beside `ReliefScaleOf`, add:

```cpp
    /** The star's angular radius seen from a body, rad: asin(R_star /
     *  distance), the cast shadow's softness (SunRadius, both materials).
     *  0 for a star or a body that is not there; pi/2 from inside the star. */
    DEEPSPACE_API double SunAngularRadius(const FSkySystem& System, int32 Body);

    /** ds.Sky.Shadows, clamped to 0..1: both materials' Shadows. */
    DEEPSPACE_API float ShadowStrength();
```

In `ShipSky.cpp`, add this to the anonymous namespace of tunables, after `CVarSunLux`:

```cpp
    /**
     * The ground's cast shadows (the developer's ruling on slice (b)'s
     * build): 1 draws them, 0 draws the unshadowed look exactly and skips
     * the march -- which is how Eyes.LandingFrame times it and
     * Eyes.ReliefLook takes its before frames. Between, a blend.
     */
    TAutoConsoleVariable<float> CVarShadows(
        TEXT("ds.Sky.Shadows"), 1.0f,
        TEXT("The ground's cast shadows: 1 drawn, 0 not (and not marched). Clamped to 0..1."));
```

In `DrawBodies`, after the `Cratering` write, add:

```cpp
            // The cast shadow: its softness is the star's own disc from here,
            // and its strength the console's.
            Instance->SetScalarParameterValue(SkyMaterial::SunRadius, static_cast<float>(ShipSky::SunAngularRadius(System, Index)));
            Instance->SetScalarParameterValue(SkyMaterial::Shadows, ShipSky::ShadowStrength());
```

In `CopyBodyLook`, replace the scalar list:

```cpp
    for (const FName Name : { SkyMaterial::Brightness, SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::ReliefScale,
                              SkyMaterial::Cratering, SkyMaterial::SunRadius, SkyMaterial::Shadows })
```

After `ShipSky::ReliefScaleOf`, add:

```cpp
double ShipSky::SunAngularRadius(const FSkySystem& System, int32 Body)
{
    if (!System.Bodies.IsValidIndex(Body) || System.Bodies[Body].Kind == ESkyBodyKind::Star)
    {
        return 0.0;
    }
    const FSkyBody* Star = System.Bodies.FindByPredicate([](const FSkyBody& Candidate) { return Candidate.Kind == ESkyBodyKind::Star; });
    if (!Star)
    {
        return 0.0;
    }
    const double Distance = (Star->Position - System.Bodies[Body].Position).Size();
    return Distance > Star->Radius ? FMath::Asin(Star->Radius / Distance) : UE_DOUBLE_HALF_PI;
}

float ShipSky::ShadowStrength()
{
    return FMath::Clamp(CVarShadows.GetValueOnGameThread(), 0.0f, 1.0f);
}
```

- [ ] **Step 6: The materials.** In `Tools/setup_sky_materials.py`, after `SHARED = CONTRACT["shared_relief"]`, add:

```python
SHADOW = CONTRACT["shadow"]

# The cast shadow's node in M_SkyBody and M_SkyGround: 0 strength skips the
# march and draws exactly the unshadowed look. HLSL only; the march is the
# shared file's.
SHADOW_CODE = (
    "if (Strength <= 0.0) { return 1.0; }\n"
    "WR_Shadow S = %s(D.x, D.y, D.z, L.x, L.y, L.z, Footprint, SeedOffset.x, SeedOffset.y, SeedOffset.z, ReliefScale, SunRadius, Samples);\n"
    "return lerp(1.0, S.Visible, saturate(Strength));\n" % SHADOW["entry"])

# The probe's: visibility, the horizon and the cost, raw.
SHADOW_PROBE_CODE = (
    "WR_Shadow S = %s(D.x, D.y, D.z, L.x, L.y, L.z, Footprint, SeedOffset.x, SeedOffset.y, SeedOffset.z, ReliefScale, SunRadius, Samples);\n"
    "return float3(S.Visible, S.Horizon, S.Bands);\n" % SHADOW["entry"])

# SunRadius where nothing writes it (an editor viewport): the Sun from 1 AU.
DEFAULT_SUN_RADIUS = 0.00465
```

After `custom_node`, add:

```python
def cast_shadow(g, direction, light, footprint, seed, relief_scale):
    """How much of its star this point of the ground sees past the ground
    (WR_SunVisible; the developer's ruling on slice (b)'s build), times
    Shadows: 1 where Shadows is 0, which skips the march. direction and
    light are in the body's axes, footprint the face's own."""
    sun_radius = g.scalar("sun_radius", DEFAULT_SUN_RADIUS)
    strength = g.scalar("shadows", 1.0)
    sources = (direction, light, footprint, seed_offset(g, seed), relief_scale, sun_radius,
               g.constant(CONSTANTS["shadow_samples"]), strength)
    return custom_node(g, SHADOW_CODE, list(zip(SHADOW["inputs"] + ["Strength"], sources)),
                       unreal.CustomMaterialOutputType.CMOT_FLOAT1)
```

In `sky_body()`, directly after `shaded = g.mul(g.mul(lambert, soft), g.constant(CONSTANTS["lambert_disc_gain"]))`, add:

```python
    # The cast shadow, in the body's axes, from the face's own D and
    # footprint. A giant's billow is not ground: Banding takes its
    # ReliefScale out, so it casts nothing.
    grounded = g.mul(knobs[3], g.unary(unreal.MaterialExpressionOneMinus, knobs[2]))
    shaded = g.mul(shaded, cast_shadow(g, direction, to_body(g, axes, light), footprint, seed, grounded))
```

Also add to its docstring's law, after `shaded = ...`: `shaded  *= lerp(1, WR_SunVisible(D, L in body axes, footprint, ...), Shadows)`.

In `sky_ground()`, directly after its `shaded = ...` line, add:

```python
    # The same shadow: the light turned into the tile's axes -- the
    # universe's, as D is -- so the handover draws one function.
    light_local = g.node(unreal.MaterialExpressionTransform,
                         transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                         transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    g.link(light, light_local)
    shaded = g.mul(shaded, cast_shadow(g, direction, g.unary(unreal.MaterialExpressionNormalize, light_local),
                                       footprint, seed, relief_scale))
```

Add its line to the docstring too. After `sky_ground_probe`, add:

```python
def shadow_probe():
    """Eyes.WorldReliefParity's shadow case: WR_SunVisible over the probe
    patch (probe_direction) for ProbeLight in body axes, at ProbeFootprint,
    untonemapped.

        pixel = ProbeBias.rgb + (visible, horizon, bands)"""
    asset = "M_SkyShadowProbe"
    material = fresh_material(asset)
    # The horizon is signed, and the template clamps emissive at 0 unless told otherwise.
    material.set_editor_property("allow_negative_emissive_color", True)
    g = Graph(material)
    seed = g.vector("surface_seed", (0.0, 0.0, 0.0, 0.0))
    relief_scale = g.scalar("relief_scale", 0.0)
    sun_radius = g.scalar("sun_radius", DEFAULT_SUN_RADIUS)
    footprint = g.scalar("probe_footprint", 0.0)
    light = g.vector("probe_light", (0.0, 0.0, 1.0, 0.0))
    bias = g.vector("probe_bias", (0.0, 0.0, 0.0, 0.0))
    direction = probe_direction(g)
    sources = (direction, mask(g, light, "rgb"), footprint, seed_offset(g, seed), relief_scale, sun_radius,
               g.constant(CONSTANTS["shadow_samples"]))
    out = custom_node(g, SHADOW_PROBE_CODE, list(zip(SHADOW["inputs"], sources)), unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    g.emissive(g.add(out, mask(g, bias, "rgb")))
    finish(material, asset)
```

In `main()`, after `sky_ground_probe()`, add `shadow_probe()`.

- [ ] **Step 7: Build, author, and run them to see them pass.**

Run:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; \
tail -12 Saved/setup_sky_materials.txt && \
./test.sh DeepSpace.Sky.MaterialContract && ./test.sh DeepSpace.Sky.ShadowParameters && ./test.sh DeepSpace.Sky && ./test.sh DeepSpace.Surface
```

Expected: the report ends `ok` and lists `M_SkyShadowProbe`. Every test passes. A Custom-node HLSL error is invisible here, because under the commandlet the translator runs and no shader compiles. Task 4's first render is what catches it.

- [ ] **Step 8: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Tools/sky_material_contract.json Tools/setup_sky_materials.py \
  Source/DeepSpace/Sky/SkyMaterialContract.h Source/DeepSpace/Sky/ShipSky.h Source/DeepSpace/Sky/ShipSky.cpp \
  Source/DeepSpace/Tests/SkyMaterialContractTest.cpp Source/DeepSpace/Tests/ShipSkyShadowTest.cpp Content/Materials/Sky && \
git commit -qm "feat(sky): M_SkyBody and M_SkyGround cast shadows through WR_SunVisible; SunRadius, Shadows, ds.Sky.Shadows

The same function, D, footprint and light on the proxy and on the tiles, so
the handover keeps it; a giant's billow casts nothing; 0 skips the march.
M_SkyShadowProbe for the parity test. The contract on three sides.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 9: Prove them.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
Tools/mutate.sh Source/DeepSpace/Sky/SkyMaterialContract.h 'ShadowSamples = 12;' 'ShadowSamples = 13;' 'DeepSpace.Sky.MaterialContract$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'SkyMaterial::Shadows, ShipSky::ShadowStrength());' 'SkyMaterial::Shadows, 1.0f);' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'FMath::Asin(Star->Radius / Distance)' 'FMath::Asin(System.Bodies[Body].Radius / Distance)' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'SkyMaterial::Cratering, SkyMaterial::SunRadius, SkyMaterial::Shadows })' 'SkyMaterial::Cratering, SkyMaterial::SunRadius })' 'DeepSpace.Sky.ShadowParameters$'; \
Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'return FMath::Clamp(CVarShadows.GetValueOnGameThread(), 0.0f, 1.0f);' 'return CVarShadows.GetValueOnGameThread();' 'DeepSpace.Sky.ShadowParameters$'; \
./build.sh
```

Expected: five `KILLED`.

`CheckShadow`'s wiring checks are asserted of authored assets, which `mutate.sh` does not rebuild, so they are proven by hand, once. In `setup_sky_materials.py`, in `sky_body()`, replace `to_body(g, axes, light)` in the `cast_shadow` call by `light`. Re-author as in Step 7 and run `./test.sh DeepSpace.Sky.MaterialContract`. Expected: FAIL, "M_SkyBody turns the light into the body's axes for it". Then `git checkout Tools/setup_sky_materials.py`, re-author, and run the test again. Expected: PASS. Confirm `git status --short Content/Materials/Sky` is clean against the commit, because the re-authored assets must be byte-identical. If they are not, commit the re-authored assets with the message "chore(sky): re-authored after the wiring proof".

---

## Task 4: THE GATE -- `Eyes.LandingFrame` times the cast shadow at 4K; the sample count; GO or NO-GO

**Owner:** T (this task only, taking `LandingFrameEyesTest.cpp` from the orchestrator's Task 39). **Depends on:** Task 3.

**Files:**
- Modify: `Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp`
- Modify, only if the gate lowers the count: `Tools/sky_material_contract.json` (`shadow_samples`), `Source/DeepSpace/Sky/SkyMaterialContract.h` (`ShadowSamples`), `Source/DeepSpace/Tests/WorldReliefShadowTest.cpp` (`Samples`)

**Interfaces:**
- Consumes: `ds.Sky.Shadows` (Task 3); `ShipSky::GotoPlacement` (Task 1).
- Produces: `Saved/Eyes/LandingFrame/report.txt`, one line per case: `<case>: <on> ms a frame with the cast shadow, <off> without (+<cost>), <n> tiles drawn`. It also produces the verdict, `GO at N = <n>` or `NO-GO`.

**The rule, decided now:** the shadow's own added cost must be at most 1.0 ms and the whole frame at most 16.6 ms, in every case, at the largest `N` of {12, 8, 6} that fits. Planning measured the quality at those three counts (*Measured while planning*, Finding 2): 8 and 6 lose 1-4 points of the truth's coverage against 12. Nothing above 12 is tried: 24 gains about 2 points for twice the cost.

- [ ] **Step 1: The A/B, and the orbit at dusk.** In `LandingFrameEyesTest.cpp`, add the includes `#include "HAL/IConsoleManager.h"` and `#include "Sky/ShipSky.h"`. Add to the header comment: "and the cast shadow's own cost in each case, the frame timed with ds.Sky.Shadows 0 -- the march skipped -- and 1, at most 1.0 ms: the terrain's spare (Eyes.TerrainBudget measured 4.96 of its 6 ms); and a third case, 200 km over Baemsekai IV at dusk, where every pixel of the frame marches." Then replace everything from `FString Report;` to the end of the `for (const double Agl ...)` loop with:

```cpp
    /** The cast shadow's own budget, ms: the terrain's measured spare. */
    constexpr double ShadowBudgetMs = 1.0;
    IConsoleVariable* Shadows = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Shadows"));
    if (!TestNotNull(TEXT("ds.Sky.Shadows exists"), Shadows))
    {
        return false;
    }
    const float ShadowsWere = Shadows->GetFloat();
    const auto TimeFrame = [&]() -> double
    {
        TArray<FColor> Pixel;
        // The sky writes this frame's Shadows into every material.
        Test.Step(1.0f / 60.0f);
        for (int32 Warm = 0; Warm < 3; ++Warm)
        {
            // A fresh editor draws a material as the engine's default until
            // its shaders compile: timing that would time the wrong frame.
            if (GShaderCompilingManager)
            {
                GShaderCompilingManager->FinishAllCompilation();
            }
            Test.World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
        }
        const double Start = FPlatformTime::Seconds();
        for (int32 Shot = 0; Shot < 20; ++Shot)
        {
            Test.Step(1.0f / 60.0f);
            Test.World->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FlushRenderingCommands();
            // One pixel read back: the GPU has finished the frame before the clock reads.
            Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixel, FReadSurfaceDataFlags(), FIntRect(0, 0, 1, 1));
        }
        return (FPlatformTime::Seconds() - Start) * 1000.0 / 20.0;
    };

    struct FCase
    {
        const TCHAR* Name;
        double AglCm;
        bool bDusk;
    };
    const FCase Cases[] = {
        { TEXT("50 km"), 5.0e6, false },
        { TEXT("1.5 m"), 150.0, false },
        // Last: it moves the ship off the line the other two share.
        { TEXT("200 km at dusk"), 2.0e7, true },
    };
    FString Report;
    for (const FCase& Case : Cases)
    {
        if (Case.bDusk)
        {
            const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, 4, Case.AglCm,
                Ship->GetFlightState().GetUniversePosition(), ShipSky::EGotoSide::Dusk);
            if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet()))
            {
                continue;
            }
            Ship->PlaceShip(Dusk->Position, Dusk->Orientation);
        }
        else
        {
            Ship->PlaceShip(Fourth.Position + Up * (Fourth.Radius + Field->Height(FVector3d(Up), 0.0) + Case.AglCm),
                            FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat());
        }
        Test.Step(1.0f / 60.0f);
        Test.Ground->FlushBuildsForTest();
        Test.Step(1.0f / 60.0f);
        // Looking out and a little down through the cockpit glass, as a pilot
        // would; at dusk down the nose, at the world's centre.
        Capture->SetWorldLocationAndRotation(PilotEye, Case.bDusk ? FRotator::ZeroRotator : FRotator(-15.0, 0.0, 0.0));
        double Ms[2] = { 0.0, 0.0 };
        for (int32 On = 0; On < 2; ++On)
        {
            Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
            Ms[On] = TimeFrame();
        }
        const double Cost = Ms[1] - Ms[0];
        const FString Line = FString::Printf(TEXT("%s: %.2f ms a frame with the cast shadow, %.2f without (+%.2f), %d tiles drawn\n%s\n"),
            Case.Name, Ms[1], Ms[0], Cost, Test.Ground->GetDrawnKeys().Num(), *Test.Ground->Describe());
        Report += Line;
        AddInfo(Line);
        TestTrue(FString::Printf(TEXT("the frame fits 16.6 ms at %s (%.2f)"), Case.Name, Ms[1]), Ms[1] <= 16.6);
        TestTrue(FString::Printf(TEXT("and the cast shadow costs at most %.1f ms of it (%.2f)"), ShadowBudgetMs, Cost), Cost <= ShadowBudgetMs);
    }
    Shadows->Set(ShadowsWere, ECVF_SetByCode);
```

- [ ] **Step 2: Run it at N = 12.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && Tools/eyes.sh Eyes.LandingFrame; cat Saved/Eyes/LandingFrame/report.txt; grep -c "error X\|Shader compile failed\|Failed to compile" Saved/Logs/DeepSpace.log`

Expected: a report of three cases, and 0 shader-compile errors. If the log shows a compile error for a Custom node, the HLSL half is wrong. Fix it in the shared file (both compilers must still take it) and re-run, since `./build.sh` passing proves only the C++ half. Read the three `+` costs.

- [ ] **Step 3: If any case's cost is over 1.0 ms, lower the count and re-run.** Set 8 in all three places: `"shadow_samples": 8` in the JSON, `ShadowSamples = 8;` in the header, `constexpr int32 Samples = 8;` in `WorldReliefShadowTest.cpp`. Then:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && \
. Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_sky_materials.py" -unattended -nopause -nosplash -NoLiveCoding; \
./test.sh DeepSpace.Sky.MaterialContract && ./test.sh DeepSpace.Surface.WorldRelief && \
Tools/eyes.sh Eyes.LandingFrame; cat Saved/Eyes/LandingFrame/report.txt
```

If it is still over, do the same at 6. `.ShadowCost` scales its bounds with `Samples`, and `.ShadowAgainstTruth`'s bounds hold at 8 and 6 (planning: ratios 0.77-0.84, mean |dv| at most 0.144).

- [ ] **Step 4: The verdict.** Write it into the test's header comment as one line, in the form the parity test keeps its verdicts:

`Cast-shadow gate (2026-09-28): GO at N = <n> -- +<a> ms at 50 km, +<b> ms at 1.5 m, +<c> ms at 200 km at dusk (frames <A>, <B>, <C> ms); N = 12 was <cost list>`

or `NO-GO -- at N = 6 +<a>, +<b>, +<c> ms`.

  - **GO:** commit (Step 5) and go on to Task 5.
  - **NO-GO:** commit the measurement (Step 5) and go to Task 4b. Do not start Task 5.
  - **A whole frame over 16.6 ms with the shadow off** is not this plan's to fix. It is the frame's own budget failing (Task 39 (Z)), so stop and report it with the numbers.

- [ ] **Step 5: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp \
  Tools/sky_material_contract.json Source/DeepSpace/Sky/SkyMaterialContract.h Source/DeepSpace/Tests/WorldReliefShadowTest.cpp Content/Materials/Sky && \
git commit -qm "test(eyes): Eyes.LandingFrame times the cast shadow at 4K, and the orbit at dusk; the gate's verdict

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Prove the new assertion.** Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Tests/Eyes/LandingFrameEyesTest.cpp 'constexpr double ShadowBudgetMs = 1.0;' 'constexpr double ShadowBudgetMs = 0.0;' Eyes.LandingFrame; ./build.sh`

Expected: `KILLED`. The shadow costs something in at least one case. If it SURVIVES, the A/B is not reaching the materials: the switch is not skipping the march, or the materials never marched. Find out before going on.

---

## Task 4b (only on NO-GO): a stop-and-replan point, not an executable task

The per-pixel march does not fit the budget at 6 samples. Choosing between the ways out changes what the ruling built, so it is the developer's to rule on. Stop, and put this to the developer with the gate's three cost lines at 12, 8 and 6:

1. **Bake it** (planning's recommendation if the gate fails). Worlds neither spin nor move, so each world's sun is fixed in its own axes, and the shadow is a fixed function of D. The same `WR_SunVisible` would be drawn by `DrawMaterialToRenderTarget` (the parity probe's own mechanism) into a clipmap centred on the ship: three levels of 512 x 512 texels over about 2, 20 and 200 km. Each level is rebuilt when the ship has moved an eighth of it, and both materials sample it. The frame then pays a few texture reads. The cost is detail finer than a texel (4 m, 40 m and 400 m at the three levels), plus a new component and a D-to-texel mapping per level. Parity would hold the baked texels to the C++ mirror just as the probe does.
2. **Fewer bands.** March only the coarsest `k` bands. On an earlier variant of the march, planning measured 6 bands at 16 samples shading 0.45 of the truth at 5 degrees on IV, where all twelve shaded 0.72. This stays per pixel, but the term becomes much weaker.
3. **Relax the budget.** 1.0 ms is the terrain's measured spare, not a ruling; the 16.6 ms frame is the ruling. If the whole frame still fits at N = 12 or 8, the developer may give the shadow more of it.

When the ruling comes back, write it into the spec under the ruling bullet and re-plan Tasks 5 and 6 against it.

---

## Task 5: parity -- `Eyes.WorldReliefParity`'s shadow leg, and the handover at dusk

**Owner:** T. **Depends on:** Task 4 (GO; `N` settled).

**Files:**
- Modify: `Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp` -- the shadow leg, its SUMMARY line, and its verdict line in the header
- Modify: `Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp` -- the dusk leg

**Interfaces:**
- Consumes: `M_SkyShadowProbe`, `SkyMaterial::{ShadowProbePath, ProbeLight, SunRadius, ShadowSamples}` (Task 3); `WorldReliefShading::SunVisible`, `WorldReliefNoise::SunVisibleF32` (Task 2); `ShipSky::SunAngularRadius`; the parity test's `Draw`, `Selecting`, `FGap`, `IsFinite`, `FloorRuleFactor`, `Footprints`, `Side`.
- Produces: report lines `Baemsekai IV's cast shadow, sun <deg> deg, footprint 1/<n>: ...` and `SUMMARY cast shadow C++-vs-GPU <x>, float-vs-double <y>`.

**The tolerance, decided now:** this is the measured floor as a rule, applied to a term that never had engine nodes. At each footprint and each sun, the GPU is held to the larger of 1e-3 and 1.25 x the file's own float build's distance from double, measured in the same run at the same D. Visibility is continuous in D (the disc's share, the fades and the exits all are), so no sample is left out. The horizon is reported, not held: an exact exit can stop float and double at different samples with the same visibility. The band counts must agree at 98% of pixels, which is how a march of the wrong shape shows. Planning's float floor: 1.1e-3 to 2.1e-3 at 1/3072, and 5.3e-3 to 1.2e-2 at 1/12288.

- [ ] **Step 1: The failing leg.** In `WorldReliefParityTest.cpp`, before the two `SUMMARY` lines, add:

```cpp
    // -- The cast shadow (the developer's ruling on slice (b)'s build) -------
    // M_SkyShadowProbe draws WR_SunVisible over the same patch for Baemsekai
    // IV under three suns above the patch's centre, toward its east -- 2
    // degrees (the penumbra's range across the patch), 10 (the dusk goto's)
    // and 60 (the exit) -- at each footprint; the C++ is
    // WorldReliefShading::SunVisible at the very D the GPU drew. Held to the
    // measured floor as a rule: 1e-3, or 1.25 x the file's own float build's
    // distance from double at that footprint and sun, whichever is larger.
    double WorstShadow = 0.0;
    double WorstShadowFloat = 0.0;
    {
        UMaterial* ShadowShared = LoadObject<UMaterial>(nullptr, SkyMaterial::ShadowProbePath);
        if (!TestNotNull(TEXT("M_SkyShadowProbe is built (Tools/setup_sky_materials.py)"), ShadowShared))
        {
            return false;
        }
        UMaterialInstanceDynamic* ShadowProbe = UMaterialInstanceDynamic::Create(ShadowShared, Test.World);
        const double SunRadius = ShipSky::SunAngularRadius(HomeSky, 4);
        const double ReliefScale = Ground.SlopeScale();
        ShadowProbe->SetVectorParameterValue(SkyMaterial::SurfaceSeed, ShipSky::SurfaceSeed(Fourth.SurfaceSeed, Fourth.BeltPairs));
        ShadowProbe->SetScalarParameterValue(SkyMaterial::ReliefScale, static_cast<float>(ReliefScale));
        ShadowProbe->SetScalarParameterValue(SkyMaterial::SunRadius, static_cast<float>(SunRadius));
        const FLinearColor NoBias(0.0f, 0.0f, 0.0f, 0.0f);
        const TArray<FVector3d> Directions = Draw(Test.World, Target, Probe, Selecting(EPass::Direction), NoBias);
        // The patch's centre and its east, from the D the probe drew.
        const int32 Middle = (Side / 2) * Side;
        const FVector3d Centre = Directions[Middle + Side / 2].GetSafeNormal();
        const FVector3d Across = Directions[Middle + Side - 1] - Directions[Middle];
        const FVector3d Level = (Across - Centre * FVector3d::DotProduct(Across, Centre)).GetSafeNormal();
        for (const double Degrees : { 2.0, 10.0, 60.0 })
        {
            const double Elevation = FMath::DegreesToRadians(Degrees);
            const FVector3d Light = (Centre * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
            ShadowProbe->SetVectorParameterValue(SkyMaterial::ProbeLight,
                FLinearColor(static_cast<float>(Light.X), static_cast<float>(Light.Y), static_cast<float>(Light.Z), 0.0f));
            for (int32 Row = 0; Row < static_cast<int32>(UE_ARRAY_COUNT(Footprints)); ++Row)
            {
                const float Footprint = static_cast<float>(Footprints[Row].Table.Footprint);
                ShadowProbe->SetScalarParameterValue(SkyMaterial::ProbeFootprint, Footprint);
                const TArray<FVector3d> Drawn = Draw(Test.World, Target, ShadowProbe, NoBias, NoBias);
                double Gap = 0.0;
                double FloatGap = 0.0;
                double HorizonGap = 0.0;
                int32 NotFinite = 0;
                int32 BandsAgree = 0;
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
                    const FSunVisibility Held = WorldReliefShading::SunVisible(Fourth.Relief, D, Light, SunRadius,
                        static_cast<double>(Footprint), SkyMaterial::ShadowSamples);
                    const FSunVisibility Float = WorldReliefNoise::SunVisibleF32(FVector3f(D), FVector3f(Light), Footprint,
                        FVector3f(Fourth.Relief.SeedOffset), static_cast<float>(ReliefScale), static_cast<float>(SunRadius), SkyMaterial::ShadowSamples);
                    Gap = FGap::Wider(Gap, FMath::Abs(Held.Visible - Pixel.X));
                    FloatGap = FGap::Wider(FloatGap, FMath::Abs(Held.Visible - Float.Visible));
                    HorizonGap = FGap::Wider(HorizonGap, FMath::Abs(Held.Horizon - Pixel.Y));
                    BandsAgree += FMath::RoundToInt32(Pixel.Z) == Held.Bands ? 1 : 0;
                    Shaded += Held.Visible < 0.5 ? 1 : 0;
                }
                const int32 Compared = Side * Side - NotFinite;
                const double HeldTo = FMath::Max(1.0e-3, FloorRuleFactor * FloatGap);
                const FString At = FString::Printf(TEXT("Baemsekai IV's cast shadow, sun %.0f deg, footprint 1/%.0f"), Degrees, 1.0 / Footprint);
                TestEqual(At + TEXT(": every pixel the GPU drew is finite"), NotFinite, 0);
                TestTrue(FString::Printf(TEXT("%s: WorldReliefShading::SunVisible computes what the GPU drew, held to %.1e (the float build %.2e from double): %.2e"),
                    *At, HeldTo, FloatGap, Gap), Gap <= HeldTo);
                TestTrue(FString::Printf(TEXT("%s: the GPU marched as the C++ did, bands alike at 98%% of pixels (%.2f%%)"),
                    *At, 100.0 * BandsAgree / FMath::Max(Compared, 1)), BandsAgree >= 0.98 * Compared);
                WorstShadow = FMath::Max(WorstShadow, Gap);
                WorstShadowFloat = FMath::Max(WorstShadowFloat, FloatGap);
                Report.Add(FString::Printf(TEXT("%s: %d compared, %.1f%% shaded, %d not finite"), *At, Compared, 100.0 * Shaded / FMath::Max(Compared, 1), NotFinite));
                Report.Add(FString::Printf(TEXT("  visibility C++ vs GPU %.2e, held to %.1e; float build vs double %.2e; horizon %.2e rad (reported); bands alike %.2f%%"),
                    Gap, HeldTo, FloatGap, HorizonGap, 100.0 * BandsAgree / FMath::Max(Compared, 1)));
            }
        }
    }
    Report.Add(FString::Printf(TEXT("SUMMARY cast shadow C++-vs-GPU %.2e, float-vs-double %.2e"), WorstShadow, WorstShadowFloat));
```

  Also add the includes `#include "Sky/ShipSky.h"` (already present) and nothing else. `FSunVisibility` comes with `Surface/WorldRelief.h`.

- [ ] **Step 2: Run it.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && Tools/eyes.sh Eyes.WorldReliefParity; grep -A1 "cast shadow" Saved/Eyes/WorldReliefParity/report.txt | head -40`

Expected: PASS, with 15 shadow blocks. At 60 degrees every gap is 0 and every band count is 0 (the exit). At 2 degrees the shaded share is about 50% at the fine footprints (planning: 46.9% to 50.1% on its made offset). The existing legs are unchanged.

The possible verdicts, and what each means:

  - **PASS:** go on.
  - **Over its allowance at one or two pixels, at 1/3072 or 1/12288 only, with the bands still alike at 98%:** a FLOAT FLOOR verdict. The GPU's float (FMA contraction, `pow` as `exp2(log2)`) sits beyond the C++ float build at a penumbra pixel near the march's first sample. Stop and report the gaps. The tolerance is not loosened.
  - **Over its allowance broadly, or bands alike under 98%:** a port bug. The GPU marched a different march. Look first at the light's axes, the footprint, and `Samples` reaching the node.

- [ ] **Step 3: The handover at dusk.** In `HandoverParityEyesTest.cpp`, add the includes `#include "HAL/IConsoleManager.h"` and `#include "Sky/ShipSky.h"`. Then, before `const FString Dir = ...`, add:

```cpp
    // The cast shadow at the handover (the developer's ruling on slice (b)'s
    // build): at 49.9 km over Baemsekai IV at a 3-degree dusk, where the
    // relief shades 40-50% of the ground, the ground's frame is still the
    // orbit's to 1e-3, and the shadow reaches both: with ds.Sky.Shadows 0 each
    // is brighter, by the same share.
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
    const TOptional<FNavPlacement> Dusk = ShipSky::GotoPlacement(Here, Index, 4.99e6, Ship->GetFlightState().GetUniversePosition(),
                                                                 ShipSky::EGotoSide::Dusk, FMath::DegreesToRadians(3.0));
    if (!TestTrue(TEXT("goto dusk places over Baemsekai IV"), Dusk.IsSet()))
    {
        return false;
    }
    double Means[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };   // [shadows][ground, orbit]
    for (int32 On = 0; On < 2; ++On)
    {
        Shadows->Set(static_cast<float>(On), ECVF_SetByCode);
        Test.Ground->SetActorHiddenInGame(false);
        Ship->PlaceShip(Dusk->Position, Dusk->Orientation);
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
        Test.Ground->SetActorHiddenInGame(true);
        Proxy->SetVisibility(true);
        Shoot(Seen);
        Means[On][1] = CentralMean(Seen);
    }
    Shadows->Set(ShadowsWere, ECVF_SetByCode);
    const double GroundShare = Means[1][0] / FMath::Max(Means[0][0], 1e-12);
    const double OrbitShare = Means[1][1] / FMath::Max(Means[0][1], 1e-12);
    const FString DuskLine = FString::Printf(TEXT("49.9 km over Baemsekai IV at a 3-degree dusk: ground %.6f (%.6f without the shadow), orbit %.6f (%.6f); the shadow keeps %.4f of the ground's light, %.4f of the orbit's"),
        Means[1][0], Means[0][0], Means[1][1], Means[0][1], GroundShare, OrbitShare);
    AddInfo(DuskLine);
    TestTrue(FString::Printf(TEXT("with the shadow the ground's frame is the orbit's to 1e-3 of it (%s)"), *DuskLine),
        FMath::Abs(Means[1][0] - Means[1][1]) <= 1.0e-3 * Means[1][1]);
    TestTrue(TEXT("the shadow reaches both frames: each is darker with it"), GroundShare < 0.99 && OrbitShare < 0.99);
    TestTrue(TEXT("by the same share, to 1e-3"), FMath::Abs(GroundShare - OrbitShare) <= 1.0e-3);
```

  Then make the report file carry both lines: replace `FFileHelper::SaveStringToFile(Line + TEXT("\n"), ...)` with `FFileHelper::SaveStringToFile(Line + TEXT("\n") + DuskLine + TEXT("\n"), ...)`.

- [ ] **Step 4: Run it.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && Tools/eyes.sh Eyes.HandoverParity; cat Saved/Eyes/HandoverParity/report.txt`

Expected: PASS, with two lines. The dusk line's shares should be well under 1. Planning expects 40-50% of the ground shaded at 3 degrees on IV, so a share of roughly 0.5-0.7 of the light kept, and the two shares within 1e-3.

- [ ] **Step 5: The verdict line, and commit.** Add this to `WorldReliefParityTest.cpp`'s header, after the T2 line:

`* Cast shadow (the developer's ruling on slice (b)'s build, 2026-09-28): PASS -- Baemsekai IV under 2, 10 and 60-degree suns at five footprints, WorldReliefShading::SunVisible vs the GPU <worst> against the float build's <worst float> from double; bands alike at <least>%; SUMMARY ...`

Fill in the run's numbers. Then:

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add Source/DeepSpace/Tests/Eyes/WorldReliefParityTest.cpp Source/DeepSpace/Tests/Eyes/HandoverParityEyesTest.cpp && \
git commit -qm "test(eyes): the cast shadow held to its C++ mirror on the GPU, and the handover keeps it at dusk

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 6: Prove them.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && \
MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Surface/WorldRelief.cpp 'FWorldRelief(Params).SlopeScale(), SunRadius, Samples);' 'FWorldRelief(Params).SlopeScale(), SunRadius * 0.5, Samples);' Eyes.WorldReliefParity; \
MUTATE_RUNNER=Tools/eyes.sh Tools/mutate.sh Source/DeepSpace/Sky/ShipSky.cpp 'SkyMaterial::Cratering, SkyMaterial::SunRadius, SkyMaterial::Shadows })' 'SkyMaterial::Cratering, SkyMaterial::SunRadius })' Eyes.HandoverParity; \
./build.sh
```

Expected: two `KILLED`.
- The half-radius mirror misses the GPU's penumbra.
- The ground no longer copies Shadows, so it stays shadowed with the switch at 0 and the shares part.

---

## Task 6: the frames after, the numbers, the docs

**Owner:** T. **Depends on:** Task 5.

**Files:**
- Modify: `docs/superpowers/specs/2026-09-27-landing-design.md` (the measured lines under *Ruled on slice (b)'s build*'s first bullet)
- Modify: `docs/superpowers/plans/2026-09-27-landing-slice-1.md` (*RULINGS AFTER PLANNING*: one line)
- Modify: `CLAUDE.md`. In *The sky*, add a paragraph after *A world's face and relief are one noise*; in *Where each tunable lives*, add a row.

- [ ] **Step 1: The frames after.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./build.sh && EYES_TAG=shadows-after Tools/eyes.sh Eyes.ReliefLook && python3 Tools/relief_look_compare.py Saved/Eyes/ReliefLook/shadows-before Saved/Eyes/ReliefLook/shadows-after`

Expected: a table of 12 frames, exit 0. Every shadows-off frame is pixel-identical to the frame before the term existed, which proves `ds.Sky.Shadows 0` is exactly the old look. Planning's predictions are listed below. A frame far from its prediction is a finding to report, not a failure:
  - At 10 degrees, orbit: coverage 0% on III, about 0.3% on IV and 1-2% on V.
  - At 10 degrees, ground: about 2% on IV and 3-5% on V.
  - At 3 degrees, orbit: under 1% on III, about 35-45% on IV and 40-50% on V.
  - At 3 degrees, ground: about 40% on IV and 50% on V.

If the comparison prints `OFF FRAME DIFFERS`, the switch is not the old look. Find out why and do not go on: the most likely cause is a Custom node that does not return exactly 1.0 at strength 0.

- [ ] **Step 2: Look at the frames.** Open `Saved/Eyes/ReliefLook/shadows-after/world_4_ground_low_shadows1.png` and its `shadows0` twin, then the same pair for `orbit_low`, then both 10-degree pairs.

  Check that the shadows fall away from the sun and lie across the view in the ground frames. Check that no speckle, banding or tile seam appears where the unshadowed frame has none (aliasing would show as noise that the shadows-off frame lacks). Check that the terminator side is darker and never brighter.

  Write a one-line judgement per world into the spec lines below. The developer's own look is carried to the next playtest.

- [ ] **Step 3: The spec.** Under the ruling's first bullet, *Cast shadows*, in `docs/superpowers/specs/2026-09-27-landing-design.md`, add an indented line using the run's numbers:

```markdown
  - **Measured, 2026-09-28** (plan `2026-09-28-landing-b-cast-shadows.md`): `WR_SunVisible`, <N> samples, the twelve detail bands at twice each sample's spacing, no craters (walls under 2 degrees on physical relief), the star's own disc. +<a> ms at 50 km, +<b> ms at 1.5 m, +<c> ms at 200 km at dusk (frames <A>/<B>/<C> ms). Parity: C++ vs GPU <worst> against the float build's <worst float>. Frames at dusk's 10 degrees, mean brightness without -> with (coverage): orbit III <x> -> <y> (<c>%), IV ..., V ...; ground III ..., IV ..., V ...; at 3 degrees: orbit ..., ground .... At 10 degrees physical relief shades little (planning: under 3%); the shadows show below about 5 degrees. The developer's look carried to the playtest.
```

- [ ] **Step 4: The parent plan and CLAUDE.md.** In `docs/superpowers/plans/2026-09-27-landing-slice-1.md`, append to the *RULINGS AFTER PLANNING* paragraph: "**Cast shadows (ruled 2026-09-28)** are their own plan, `2026-09-28-landing-b-cast-shadows.md`, owned by track T."

  In `CLAUDE.md`, in *The sky*, after the paragraph that begins "**A world's face and relief are one noise.**", add:

```markdown
**The ground casts shadows**, marched through the same heights toward the
sun (`WR_SunVisible` in `WorldRelief.ush`; the developer's ruling on slice
(b)'s build). `M_SkyBody` and `M_SkyGround` call it with the same D,
footprint and light -- in the body's axes on the proxy, the tile's on the
ground -- so the 50 km handover keeps it (`Eyes.HandoverParity`). It reads
the GPU's twelve detail bands, each of its samples at twice its spacing
(the face's own fade, so it never aliases), and no craters: their walls are
under 2 degrees on physical relief. Its softness is the star's own disc
(`ShipSky::SunAngularRadius`), and a shadow is black: there is no sky light.
Physical relief casts little under the dusk goto's 10 degrees and a great
deal under 5 and below. `ds.Sky.Shadows 0` draws the unshadowed look exactly
and skips the march, which is how `Eyes.LandingFrame` times it (at most
1 ms, the terrain's spare). `Eyes.WorldReliefParity` holds it to
`WorldReliefShading::SunVisible`; the sample count is the contract's
`shadow_samples`.
```

  In *Where each tunable lives*, after the `ds.Sky.SurfaceDetail` row, add:

```markdown
| `ds.Sky.Shadows` | 1 (0 draws the unshadowed look and skips the march) | `ShipSky.cpp` |
```

  Add `shadow_samples` to the named-constants sentence at the table's foot: "the cast shadow's `shadow_samples` (the contract's JSON and `SkyMaterial::ShadowSamples`, chosen by the budget gate)".

- [ ] **Step 5: The whole suite.**

Run: `cd /home/matt/Development/deepspace/.worktrees/landing-b-t && ./test.sh DeepSpace && Tools/eyes.sh Eyes.WorldReliefParity && Tools/eyes.sh Eyes.HandoverParity && Tools/eyes.sh Eyes.LandingFrame && python3 Tools/test_relief_look_compare.py`

Expected: all green, with no dirty-log gate tripped.

- [ ] **Step 6: Commit.**

```bash
cd /home/matt/Development/deepspace/.worktrees/landing-b-t && git add CLAUDE.md docs/superpowers/specs/2026-09-27-landing-design.md docs/superpowers/plans/2026-09-27-landing-slice-1.md && \
git commit -qm "docs: cast shadows -- measured, and the ground casts shadows in CLAUDE.md

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

**Done when:**
- `Eyes.LandingFrame` passes at 4K with the shadow at most 1.0 ms in each of its three cases and every frame at most 16.6 ms.
- `Eyes.WorldReliefParity` holds the cast shadow to its C++ mirror at five footprints under three suns.
- `Eyes.HandoverParity` holds the ground to the orbit at a 3-degree dusk with the shadow on, and the shadow reaches both.
- The before/after frames of Baemsekai III, IV and V are in `Saved/Eyes/ReliefLook/shadows-{before,after}`: from 200 km and at the ground, at dusk's 10 degrees and at 3. Each has its mean brightness without and with the term and its shadow coverage, recorded in the spec, and every shadows-off frame is pixel-identical to its before.
- The orchestrator merges `feat/landing-b-t` into `feat/landing-b`.

---

## Self-review

- **The ruling's parts, each owned:**
  - The shared file's term: Task 2.
  - Used by both materials: Task 3.
  - The C++ mirror and parity: Tasks 2 and 5.
  - The GPU cost in `Eyes.LandingFrame`: Task 4.
  - The before/after frames: Tasks 1 and 6.
  - Physical heights, not exaggerated: `.ShadowHeight` holds the march's height to `FWorldRelief::Height`.
- **The task's design points, each stated:**
  - Sample count, step growth, bands, footprint rule, softness and parity tolerance: *Measured while planning* (Finding 2), and Tasks 2 and 5.
  - How it combines: `shaded x lerp(1, visible, Shadows)`, multiplying the slope shading's `gain x saturate(N.L) x smoothstep`, with no fill light. Task 3's `sky_body` and `sky_ground`.
  - Where it applies: both materials at every altitude. Task 3, and `Eyes.HandoverParity` in Task 5.
  - The budget: Task 4's rule and gate.
  - The done-when: Task 6.
- **Names across tasks:** `WR_SunVisible`'s 13 arguments are the same in the file, in `SunVisibleF64`/`F32`, in `SHADOW_CODE` and in `SkyMaterial::ShadowInputs()` (the first seven pins, plus `Strength` in the materials). `FSunVisibility {Visible, Horizon, Bands}` is the same in Tasks 2 and 5. `ShipSky::SunAngularRadius`, `ShipSky::ShadowStrength` and `ds.Sky.Shadows` are the same in Tasks 3, 4, 5 and 1 (by name lookup). `ShadowSamples`, `shadow_samples` and the tests' `Samples` change together, only in Task 4.
- **No test path is a parent of another.** The new leaves are `DeepSpace.Surface.WorldRelief.{ShadowHeight, ShadowGradientMax, SunDisc, ShadowAgainstTruth, ShadowCost}`, `DeepSpace.Sky.GotoDuskElevation` and `DeepSpace.Sky.ShadowParameters`. Every mutation filter is anchored with `$`.
