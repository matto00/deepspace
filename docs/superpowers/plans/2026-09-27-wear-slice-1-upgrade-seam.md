# Wear and Upgrades Slice 1: the Upgrade Seam -- Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (- [ ]) syntax for tracking.

**Goal:** A part is a module in a bay, and it changes a number. The ship gets six fixed bays and two auxiliary slots. The reactor becomes a part (`DA_Reactor_Stock`, 1400 W), and the lights clash is resolved: one Lights part owns both the fittings' draw and the consumer's want, and draws are booked by bay. Every rated value is derived from the fitted parts, and four CVars become session overrides. Two example upgrades, `Reactor.TwinCore` (1800 W) and `Drive.QuickLever` (4.5 notches a second), are installable from the console. The engineering console shows one nameplate per fitted part; its DRAWN and SPARE lines are gone. The stock ship stays bit for bit today's ship: 1400 W supply, 1370 W at rest, a 45 s charge, 12 ly. Everything is testable headlessly. The last task re-plans the landing plan's slice (b) steps that this slice invalidates.

**Architecture:**
- **The pure core.** `Ship/ShipParts.*` holds the bays (`EShipBay`), the ratings (`EShipRating`, `FShipRatings::Stock()`), the rule for a CVar over a part (`ShipParts::Effective`), decision 7's catalogue rules (`ShipParts::Validate`), and the plain, serialisable state (`FShipPartState`, `FShipBayState`, `FShipLoadoutState`). It has no `UObject`.
- **The data.** `Tools/ship_parts.json` is the one source. `Tools/setup_ship_parts.py`, run as an editor commandlet, authors `Content/Ship/Parts/DA_*.uasset`, the catalogue `DA_ShipCatalogue` (`UShipPartCatalogue`) and `BP_DeepSpaceGameMode`'s `StartingModules`.
- **The subsystem.** `UShipSubsystem` holds one `FShipLoadoutState` and a map from part id to asset. It derives `FShipRatings` on every call and stores none of it. It pushes the supply and the lights' want on a fit. `ApplyAllocation` is the one writer of the boosters' want. `FindPart` loads the catalogue named by one ini line.
- **The console.** `UEngineeringConsoleWidget` prints nameplates read from each fitted part. The `ds.Ship.*` commands fit parts, list spares and describe the bays.

All logic is C++. Blueprints and data assets only carry data, and that data is written by script.

**Tech Stack:** Unreal Engine 5.8.2 C++ (module `DeepSpace`); UMG built in C++; Python editor scripting (`Tools/setup_ship_parts.py`, `Tools/check_blueprints.py`); automation tests (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) run through `./test.sh`, proven by `Tools/mutate.sh`.

**Spec:** /home/matt/Development/deepspace/docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md (approved 2026-09-27, every sign-off item as recommended). Decision and sign-off numbers below are the spec's. This plan covers **slice 1 only**, including sign-off 29 (slice 1 goes before landing slice (b) and re-plans the landing steps it invalidates, with one writer of the boosters' want). Slices 2-4 are not planned here.

## Global Constraints

- **The stock ship is today's ship** (ruling 1):
  - 1400 W supply;
  - 620 W of draws: life support 300 W, lights 120 W, sensors 200 W, read from the hand-made assets before anything is rewritten;
  - lights want 300 W, boosters want 450 W, boosters push 2 km/s^2 (`2.0e5` cm/s^2);
  - the jump winds on 380 W in 45 s;
  - the drive follows at 3 notches a second, and the chart reaches 12 ly.
  At rest the ship asks 620 + 300 + 450 = 1370 W. Every pre-existing test stays green on the same figures. A test changes only in where it reads a value.
- **Bays** (decision 1): `EShipBay { None, Reactor, Drive, Boosters, Lights, LifeSupport, Sensors, Aux1, Aux2 }`.
  - Each bay holds one part. A part whose `Bay` is `None` is refused. A bay is saved by name, never by position.
  - An aux part declares `Aux1` and fits either auxiliary slot.
- **An empty core bay reads `FShipRatings::Stock()` and draws nothing** (decision 3). A core part is only swapped, never removed. A bare test world is exactly today's bare test world.
- **Draws are booked by bay**, under `ShipBay::DrawKey`, e.g. `Bay.Lights`. Test loads go under `Load.<name>` and the fold under `Nav.Fold`. Nothing is booked by part id.
  - Part ids are `<Bay>.<Name>`.
  - The consumer ids (`Power.Lights`, `Power.Boosters`, `Power.Engine`) do not change.
- **One writer of the boosters' want** (sign-off 29): `ApplyAllocation`, from the fitted part's `BoostersWant`. `FitPart` never writes it. Landing slice (b) adds its `HoldWant` there.
- **The four CVars** `ds.Nav.RangeLy`, `ds.Nav.ChargeSeconds`, `ds.Nav.WindingWant` and `ds.Drive.Response` default to `-1`, which means "the fitted part's". Any value of 0 or more overrides the part for the session.
  - `ShipParts::Effective(Rated, CVar)` is the rule.
  - It is applied only in `GetChartRangeLy()`, `GetChargeSeconds()`, `GetWindingWant()` and `GetDriveResponse()`.
- **No part rates a top.** `ds.Drive.Top` and `FShipFlightLimits::Cruise()`'s tops are unchanged.
- **`Tools/ship_parts.json` is the catalogue's one source.** Assets are authored from it by `Tools/setup_ship_parts.py` and are never edited by hand. `DeepSpace.Ship.Parts.Contract` holds the JSON, the assets and `Stock()` equal.
- **Decision 7 holds over the whole catalogue.** It is enforced by `ShipParts::Validate`:
  - every combination is whole at rest under the stock reactor;
  - every upgrade is at least as open as stock on every axis of its bay;
  - no part dominates another in its bay (equal numbers do not dominate);
  - a core part rates only its own bay's values;
  - an aux part rates nothing and draws nothing at rest.
  - The spec's `.CatalogueRules` also lists **"no two aux parts alike"**. It is **deferred, not dropped**: every aux part rates nothing and draws nothing, so "alike" cannot be defined on the numbers, and slice 1 has no verb field to compare. Two aux rows under one id are already refused by the shared-id rule. The rule is written with the first aux part's spec, which gives an aux part its verb (recorded in *Self-review*).
- **Nameplates** (decision 9): one line per fitted part, in bay order, `BAY  Name  figure unit  Words`. Aux lines carry no figure.
  - Every figure is the part's own number. No CVar, flight limit, live power value or symptom ever moves a figure.
  - No `%`, tier, comparison, total drawn, headroom, charge time or symptom appears anywhere on the console.
  - An empty slot has no line.
- **Out of scope for slice 1:** aux parts, the save, wear, housings and the install act. The `FShipPartState` wear fields exist and stay zero.
  - **The HUD's `SPARE` is deliberately kept.** `UI/ShipHUDWidget.cpp:290-293` prints `"%.0f W  SPARE %.0f W"` from `GetReactorOutput()` and `GetPowerHeadroom()`. The spec removes DRAWN and SPARE from the *console* only (decision 9, sign-off 11), though its reason -- a figure waiting to be spent, which becomes the aux slots' budget once an aux verb draws -- applies to the HUD too. No aux part exists in slice 1, so the HUD line changes nothing here but its figure (430 W spare under the twin core). This plan does not touch `ShipHUDWidget.*`; the question goes to the developer (see *Execution order*, *For the developer*). If the developer rules it out, it is a one-line change to `ShipHUDWidget.cpp` that landing (b)'s track S, which owns `UI/ShipHUDWidget.*` from its Task 28, or a later wear slice, takes.
- **Anti-chore:** nothing added here changes with time. There is no timer and no charge display. The console shows no totals.
- **Build and test only through the scripts:** `./build.sh`, `./test.sh <path>` and `Tools/mutate.sh`, all behind `Tools/ue_lock.sh`, with the editor closed.
  - Authoring commandlets run only through `ue_locked`.
  - Use at most 3-4 workers and `nice -n 19` for any local sweep.
- **Every new test is proven able to fail with `Tools/mutate.sh`.** Every test path is a sibling with no children. Test files use a named local namespace (`<File>Local`), because unity builds are off.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

These are the five classes of input the spec implies that none of its named tests exercise. They are the most likely to bite first. Each has a pinning test in its owning task.

1. **A CVar at 0, or at a negative value other than -1.**
   - `ds.Nav.ChargeSeconds 0` is how five existing tests make the jump instant, so 0 must override: `ShipHUDNoseCaretTest.cpp:185` (`DeepSpace.UI.NoseCaret`), `ShipCounterFrameJumpTest.cpp:112` (`DeepSpace.Ship.CounterFrameJump`), `ShipHUDSpeedTest.cpp:334` (`DeepSpace.UI.HUDSpeed`), `ShipHUDBearingTest.cpp:110` (`DeepSpace.UI.HUDBearing`) and `TargetOverlayTest.cpp:398` (`DeepSpace.UI.TargetOverlay`). Three more set a small positive charge (`HumComponentTest.cpp:232` and `SystemMapScreenTest.cpp:553`, `:592`, 0.5 s; `NavScreenTest.cpp:249`, 2 s). Task 6 (S1) Step 9 runs all of them. A `-5` typed at the console must read the part, not a negative number.
   - A `CVar > 0` test for "set" breaks the first case silently. A `CVar != -1` test breaks the second.
   - Pinned by `DeepSpace.Ship.Parts.Arithmetic`, pure, in **Task 2 (D1)**, and by `DeepSpace.Ship.Parts.CVarOverrides`, through the ship's four getters and the flight's limits, in **Task 6 (S1)**. `Arithmetic`'s mutant turns `>=` into `>` in `ShipParts::Effective`; `CVarOverrides`' mutant bypasses the rule in a getter.
2. **Fitting the part a bay already holds** (`ds.Ship.Install Reactor.Stock` on a stock ship). Treated as a swap, it makes a spare of the same part on every repeat and books its draw twice.
   - Pinned in `DeepSpace.Ship.Parts.OnePerBay` (**Task 7 (S2)**): no spare appears and no draw doubles.
   - Pinned again by id in `DeepSpace.Ship.Parts.Commands` (**Task 11 (S6)**).
3. **The auxiliary slots when both are full, when a part is already in the other slot, and when a slot is removed.** The spec names the rules (decision 8's 3 and 5, and `Aux1` replaced when both are full), but `.OnePerBay` as specified tests only core bays.
   - Pinned in `.OnePerBay` (**Task 7 (S2)**) with aux parts made in code: the replace, one-of-a-kind, `RemovePart` of each kind, and a core bay's refusal.
4. **A part with no id, or with another part's id.**
   - `PartId == None` is how the loadout says "empty bay". So a nameless part that was accepted would book its draw and still read stock ratings.
   - A different asset under a known id would silently change what a fitted id resolves to.
   - Pinned in `.OnePerBay` (**Task 7 (S2)**): both are refused and displace nothing.
5. **The wants across a fit: lights switched off, and the boosters' one writer.**
   - A lights part fitted while the lights are off must ask nothing until they are switched on, and then ask its own want.
   - A boosters part fitted must not write the want at the fit, but on the next `ApplyAllocation`. That timing is sign-off 29's rule, and landing (b) builds its hold on it.
   - Pinned by `DeepSpace.Ship.Parts.WantsFollowTheFit` (**Task 7 (S2)**).

**RULED 2026-09-27 (after planning):** the HUD's `SPARE` line (`ShipHUDWidget.cpp:290-293`) is removed too, for the same reason as the console's (a spare figure is a budget to fill, and it grows with every reactor). Task 9 (S4) removes it, with its tests updated; `UI/ShipHUDWidget.*` joins track S's files for that task (landing (b)'s track S edits the HUD later, on top of this).

## Execution order and file ownership

**Two tracks, two worktrees, one merge** (spec, *Parallel tracks*):

| Track, tree, branch | Tasks | Owns |
|---|---|---|
| orchestrator (main checkout) | 1 (A0); the final merge in 14 (Z) | git only; this plan's commit |
| **D: parts data**, `.worktrees/wear-1-d`, `feat/wear-1-d` | 2-5 (D1-D4) | `Source/DeepSpace/Ship/ShipParts.h` and `.cpp` (**first**: the header S builds on), `Ship/ShipModuleDataAsset.h` and `.cpp`, `Ship/ShipPartCatalogue.h`, `Tests/ShipPartsTest.cpp`, `Tests/ShipPartsJson.h`, `Tools/ship_parts.json`, `Tools/setup_ship_parts.py`, `Content/Ship/Parts/*` (with `DA_ShipCatalogue`), `Content/Ship/Modules/*` (removed by the script), `Content/Blueprints/BP_DeepSpaceGameMode.uasset` (written by the script only) |
| **S: subsystem and console**, `.worktrees/wear-1-s`, `feat/wear-1-s` | 6-14 (S1-S8, Z) | `Ship/ShipSubsystem.h` and `.cpp`, `Core/DeepSpaceGameMode.h` and `.cpp`, `Config/DefaultGame.ini` (one new section), `Ship/ShipHumComponent.cpp`, `Tests/StockShip.h`, `UI/EngineeringConsoleWidget.h` and `.cpp`, `Ship/ShipConsole.cpp`, `UI/NavigationWidget.cpp`, `Tests/ShipLoadoutTest.cpp`, `Tests/LampPanelsDimTest.cpp`, `Tests/ShipSkyTest.cpp`, `Tests/HumComponentTest.cpp`, `Tests/NavScreenTest.cpp`, `Tests/ChartLayoutTest.cpp`, `Tests/SliceChooseTest.cpp`, `Tests/JumpWindsTest.cpp`, `Tests/ShipJumpTest.cpp`, `Tests/ShipScreensAgreeTest.cpp`; the landing plan's amendment (13); and in 14 (Z), `CLAUDE.md`, `Ship/ShipPowerState.h` (a comment), `docs/decisions/0003-ship-state-as-subsystem.md`, the navigation spec's fake, the wear spec's status line |

S branches from D1's commit and merges D twice more: before S2, **D2's commit by hash** (never the branch tip, which may already hold D4's asset rewrite), for D2's asset fields; and before S3, the tip, for D4's assets. D's files are only ever D's, so those merges cannot conflict.

**Files this plan shares with landing slice (a)**, which is being built in `.worktrees/landing-a-procgen` (track P, `feat/landing-a-procgen`, merged to `main` as `d1cfbc8` and deleted) and `.worktrees/landing-a-relief` (track R, `feat/landing-a-relief`) from `docs/superpowers/plans/2026-09-27-landing-slice-1.md`. No file is ever edited by both plans at once:

| File | Landing (a) writes it in | This plan writes it in | Order |
|---|---|---|---|
| `Config/DefaultGame.ini` | P: Task 3 (P2), the relief priors in `[/Script/DeepSpace.ProcGenPriorsConfig]` | Task 10 (S5): a new `[/Script/DeepSpace.ShipSubsystem]` section | **`feat/landing-a-procgen` merges to `main` first.** Task 10 starts only after that, with `main` merged into `wear-1-s` (its Step 1 is the gate). |
| `CLAUDE.md` | P: Task 3 (P2) Step 8 (*The universe*'s priors paragraph). R: Task 6 (R1) Step 18 (*Commands*' first block, the `mutate.sh` paragraph), Task 11 (R4) Step 7 (*The sky*'s material-contract paragraph), Task 12 (R5) Step 5 (*Architecture*, after the `Universe/` bullet). | Task 14 (Z): *Architecture* after the `Ship/ShipDressing*` bullet; a paragraph after the power paragraph; a new section before `## Playtest console`; the playtest block; four rows of the tunables table and its *not CVars* paragraph | **P merges to `main` first** (it has: `d1cfbc8`). R's four edits are to other paragraphs than wear's, so the hunks never touch, and R need not merge first. Task 14 starts only when no R task that edits CLAUDE.md is in progress: `landing-a-relief` has no uncommitted change to `CLAUDE.md` (its Step 1 checks). Whichever merges second merges the other's `main` first. |
| `docs/superpowers/plans/2026-09-27-landing-slice-1.md` | Amended in place as landing (a) runs (plans are "updated in place as reality contradicts them"); in particular, R's spike stopped on **FLOAT FLOOR** (`8d66dbd`), which the landing plan routes to the developer and then to a re-plan of R and of slice (b)'s shader tasks (T1, T2, T7) | Task 13 (S8), the amendment sign-off 29 requires | **R's verdict settled first, not R merged:** the developer has ruled on the FLOAT FLOOR report, and any re-plan of the landing plan that ruling causes is committed on `main`, with no landing orchestrator holding an uncommitted edit to the file. R may never merge in its present form, and landing (b) waits on this slice, so waiting on R's merge could stall both. Task 13 merges `main` immediately before it edits (its Step 1), and its commit is the last one before Task 14, whose Step 1 checks the file has not moved on `main` since. |
| `Tools/mutate.sh`, `rebuild.sh`, `launch.sh` | R (the runner hook, the second module) | never edited; only run | none needed |

Everything else this plan touches, landing (a) does not: `Universe/*`, `Surface/*`, `Sky/*`, `Shaders/*`, `GenPriors.*` and the sky's contract are not in this plan.

**Against the atmosphere plan** (`docs/superpowers/plans/2026-09-27-atmosphere-optics-model.md`, tracks O in `.worktrees/air-optics` and G in `.worktrees/air-procgen`). One file is shared:

| File | Atmosphere writes it in | This plan writes it in | Order |
|---|---|---|---|
| `Config/DefaultGame.ini` | G: Task 9 (G2), air lines inside `[/Script/DeepSpace.ProcGenPriorsConfig]` (today lines 18-89), gated only on landing P merged | Task 10 (S5): a new `[/Script/DeepSpace.ShipSubsystem]` section appended at the end of the file, after `[/Script/DeepSpace.ShipDressingConfig]` | The two edits are **non-adjacent hunks** (a whole section, `ShipDressingConfig`, lies between them), so either branch merges cleanly after the other. They still never happen at once: Task 10 Step 1 checks that `air-procgen` holds no uncommitted change to the file, and waits until G2 has committed if it does. Whichever of `feat/air-procgen` and `feat/wear-1-s` merges to `main` second merges `main` first. |

Nothing else is shared: the atmosphere plan does not edit `CLAUDE.md`, and it touches no `Ship/*`, `UI/*` or test file this plan edits.

**Against landing slices (b) and (c):**
- Every file landing (b)'s track S owns (`ShipSubsystem.*`, `ShipPowerState.*`, `ShipHum*`) is edited here **first**.
- Two test files this plan rewrites are edited by landing later: `Tests/ShipSkyTest.cpp` (landing Task 32 (T2), the relief knobs at about lines 390-392 and 433-435; this plan's Task 8, the hog block at about 667-687) and `Tests/JumpWindsTest.cpp` (landing Task 43 (C4), a landed block before `Fresh->EndPlay(EEndPlayReason::Quit);` on `Default->`; this plan's Task 8, `Default->InstallModule(Module);` becoming `Default->FitPart(Module);`). This plan merges first; the landing steps apply unchanged (Task 13 records why).
- `feat/wear-1-s` merges to `main` before landing's Task 13 (B0) creates `feat/landing-b`.
- Task 13 (S8) here adds that check to B0.

**Order:**

```
Task 1 (A0)  main clean, this plan committed, the D tree, the baseline suite
  D:  2 (D1) -> 3 (D2) -> 4 (D3) -> 5 (D4)
  S:  6 (S1)                 [after D1: the S tree is cut from D1's commit, by hash]
      -> 7 (S2)              [merge D2's commit, by hash: needs D2's Bay/Ratings fields, not D4's assets]
      -> 8 (S3) -> 9 (S4)    [merge the feat/wear-1-d tip: needs D4's assets and BP list]
      -> 10 (S5)             [GATE: feat/landing-a-procgen merged to main (it is), and air-procgen
                              not mid-edit on Config/DefaultGame.ini; merge main]
      -> 11 (S6) -> 12 (S7)
      -> 13 (S8)             [GATE: R's FLOAT FLOOR verdict settled and any landing re-plan it causes
                              committed on main; merge main right before editing]
      -> 14 (Z)              [GATE: landing plan unmoved on main since 13; no R task editing CLAUDE.md
                              in progress; merge feat/wear-1-d and main a last time; the suite; merge to main]
  Landing Task 13 (B0) opens only after feat/wear-1-s is on main.
```

S1 and S2 can run while D3 and D4 run. S3 onward needs D whole. Task 13's commit is the last commit on `feat/wear-1-s` before Task 14, and Task 14 follows it at once, so the landing plan is held off `main` for as short a time as possible.

**For the developer** (nothing here blocks slice 1 from starting):
1. **R's FLOAT FLOOR report.** Task 13 waits on the ruling and on the landing re-plan it causes being on `main`. `146ddbe` records a spec ruling ("parity per footprint and per term, slopes at the float floor at the finest two"); Task 13 still waits for the landing *plan* to be re-planned against it.
2. **The HUD's `SPARE` line** (`ShipHUDWidget.cpp:290-293`): keep it, reduce it to the reactor's rating, or remove it. The spec's argument against the console's SPARE applies to it; the spec does not name it, so this plan leaves it (*Global Constraints*, *Out of scope*).
3. **"No two aux parts alike"** is deferred to the first aux part's spec, which must say what "alike" means once an aux part has a verb (*Global Constraints*).

## Interfaces at the seams

Every name below is used exactly as written in every task.

```cpp
// Source/DeepSpace/Ship/ShipParts.h -- Task 2 (D1); Validate and the axes in Task 4 (D3)
UENUM(BlueprintType) enum class EShipBay : uint8 { None, Reactor, Drive, Boosters, Lights, LifeSupport, Sensors, Aux1, Aux2 };
UENUM(BlueprintType) enum class EShipRating : uint8 { ReactorWatts, WindingWant, ChargeSeconds, DriveResponse, BoostersWant, LinearAcceleration, LightsWant, RangeLy };
struct FShipPartSpec { FName Id; EShipBay Bay = EShipBay::None; double Draw = 0.0; TMap<EShipRating, double> Ratings; };
struct FShipRatings { double ReactorWatts, WindingWant, ChargeSeconds, DriveResponse, BoostersWant, LinearAcceleration, LightsWant, RangeLy;
                      static FShipRatings Stock(); double Get(EShipRating) const; void Set(EShipRating, double); };
USTRUCT() struct FShipPartState { FName PartId; double AgeJumps; double LifeJumps; bool bHasLife; FName Symptom; int32 Repairs; bool bOriginal; };
USTRUCT() struct FShipBayState { FName Bay; FShipPartState Part; int32 LivesDrawn; };
USTRUCT() struct FShipLoadoutState { TArray<FShipBayState> Bays; TArray<FShipPartState> Spares; };
namespace ShipBay {
    FName Name(EShipBay); TOptional<EShipBay> FromName(FName); bool IsCore(EShipBay); bool IsAux(EShipBay);
    const TArray<EShipBay>& All(); FName DrawKey(EShipBay); FName StockPartId(EShipBay); FString PlateLabel(EShipBay); }
namespace ShipParts {
    inline constexpr double StockReactorWatts = 1400.0, StockLightsWant = 300.0, StockBoostersWant = 450.0,
        StockLinearAcceleration = 2.0e5, StockWindingWant = 380.0, StockChargeSeconds = 45.0, StockDriveResponse = 3.0, StockRangeLy = 12.0;
    FName RatingName(EShipRating); TOptional<EShipRating> RatingFromName(const FString&); const TArray<EShipRating>& AllRatings();
    EShipBay OwnerOf(EShipRating); void Apply(FShipRatings& Into, const TMap<EShipRating, double>& Rated);
    FShipRatings RatingsOf(const TArray<FShipPartSpec>& Fitted); double Effective(double Rated, float CVar);
    FShipLoadoutState EmptyLoadout(); FShipBayState* FindBay(FShipLoadoutState&, EShipBay); const FShipBayState* FindBay(const FShipLoadoutState&, EShipBay);
    struct FAxis { bool bDraw; EShipRating Rating; bool bHigherIsOpen; };                       // D3
    TArray<FAxis> AxesOf(EShipBay); double Openness(const FShipPartSpec&, const FAxis&);       // D3
    double AtRestWatts(const FShipPartSpec&); TArray<FString> Validate(const TArray<FShipPartSpec>&); }   // D3

// Ship/ShipModuleDataAsset.h -- Task 3 (D2)
EShipBay UShipModuleDataAsset::Bay = EShipBay::None; FText Words; TMap<EShipRating, double> Ratings; FShipPartSpec GetSpec() const;
// Ship/ShipPartCatalogue.h -- Task 3 (D2)
class UShipPartCatalogue : public UPrimaryDataAsset { TArray<TSoftObjectPtr<UShipModuleDataAsset>> Parts; };

// Ship/ShipSubsystem.h -- S1-S7
FShipRatings GetRatings() const;                                        // S1 (stock), S2 (from the bays)
float GetWindingWant() const; float GetChargeSeconds() const; float GetDriveResponse() const; float GetChartRangeLy() const;   // S1
bool FitPart(UShipModuleDataAsset* Part); bool RemovePart(EShipBay Bay); UShipModuleDataAsset* GetFittedPart(EShipBay Bay) const;
const FShipLoadoutState& GetLoadoutState() const; const TArray<FShipPartState>& GetSpares() const;
void AddLoad(FName Name, float Watts); bool RemoveLoad(FName Name);    // S2
TArray<UShipModuleDataAsset*> GetInstalledModules() const;             // S3 (InstallModule/RemoveModule retired)
UShipModuleDataAsset* FindPart(FName PartId) const; TArray<UShipModuleDataAsset*> GetCatalogue() const; bool FitPartById(FName PartId);   // S5
bool AddSpare(FName PartId); void ClearSpares();                        // S6
int32 RestoreLoadout(const FShipLoadoutState& State);                  // S7

// UI/EngineeringConsoleWidget.h -- Task 9 (S4)
FText GetReadoutText() const; void RefreshFromShip(); FText GetShownText() const;
static FString Nameplate(EShipBay Bay, const UShipModuleDataAsset& Part); static constexpr float PlateSize = 13.0f;
```

- Tests: `Tests/ShipPartsJson.h` (D3): `ShipPartsJson::FRow {FShipPartSpec Spec; FString Asset, Name, Words;}`, `FCatalogue {FString Directory, CatalogueAsset; TArray<FRow> Rows; TArray<FString> Problems;}`, `FCatalogue Read()`, `TArray<FShipPartSpec> Specs(const FCatalogue&)`, `FString ObjectPath(const FCatalogue&, const FString& Asset)`.
- Commands (`ShipSubsystem.cpp`): `ds.Ship.Install <part>` (S6), `ds.Ship.Spares [give <part> | clear]` (S6), `ds.Ship.Describe` (S6).
- Ini (S5): `[/Script/DeepSpace.ShipSubsystem] CatalogueAsset=/Game/Ship/Parts/DA_ShipCatalogue.DA_ShipCatalogue`.
- Assets (D4): `/Game/Ship/Parts/DA_{Reactor_Stock, Reactor_TwinCore, Drive_Stock, Drive_QuickLever, Boosters_Stock, Lights_Stock, LifeSupport_Stock, Sensors_Stock, ShipCatalogue}`.

## Conventions for every task

- **Trees.** Every command names its tree (`cd /home/matt/Development/deepspace/.worktrees/<tree> && ...`), because an agent's working directory is reset between calls.
- **The lock and the editor.** Build only with `./build.sh`, test only with `./test.sh <path>`, mutate only with `Tools/mutate.sh`. Run the authoring commandlets only through `ue_locked`. The editor stays closed. After any header change on a tree where an editor was open, run `./rebuild.sh --force` first.
- **Mutation.**
  - `Tools/mutate.sh FILE 'exact old text' 'new text' FILTER` must print `KILLED`.
  - It refuses uncommitted files, so every mutation comes after its task's commit.
  - A surviving mutant means the test is strengthened and re-committed, never the mutation weakened.
  - Run `./build.sh` after every mutation run: its last build held the mutant.
- **Tests are siblings.** `DeepSpace.Ship.Parts` is a group and never a test. Every name below is a leaf under it.
- **Line numbers** are approximate, from `e5d257c`. Search for the quoted code, not the number.
- **Headers that Blueprints are built against** (`UShipModuleDataAsset`, `UShipSubsystem`, `AShipConsole`) change here. Task 8 (S3) removes two `UFUNCTION`s, so it runs `Tools/check_blueprints.py`, and so does Task 14 (Z).

---

## Task 1 (A0): Preconditions, this plan, and the D tree

**Owner:** orchestrator. **Depends on:** nothing.

**Files:** none (git only), and this plan's first commit.

- [ ] **Step 1: Check main**

```bash
cd /home/matt/Development/deepspace && git status --short && git log --oneline -1 main
```

Expected: status shows `?? docs/superpowers/plans/2026-09-27-wear-slice-1-upgrade-seam.md`, and `main` is at `e5d257c` or a descendant of it. Other untracked plans under `docs/superpowers/plans/` may be listed too (today, `?? docs/superpowers/plans/2026-09-27-atmosphere-optics-model.md`): they are other plans' and are left alone -- never added, moved or committed here. Any other line -- a modified or staged file, or anything untracked outside `docs/superpowers/plans/` -- stops here: it belongs to someone.

- [ ] **Step 2: Commit this plan on main**

```bash
cd /home/matt/Development/deepspace && git add docs/superpowers/plans/2026-09-27-wear-slice-1-upgrade-seam.md && \
git commit -m "$(cat <<'EOF'
docs: wear and upgrades slice 1 plan -- the upgrade seam, before landing (b)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

Expected: one file changed.

- [ ] **Step 3: Create the D tree**

```bash
cd /home/matt/Development/deepspace && git worktree add .worktrees/wear-1-d -b feat/wear-1-d main
```

Expected: `Preparing worktree (new branch 'feat/wear-1-d')`.

- [ ] **Step 4: Prove the tree builds and the suite is green before anything changes**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh && ./test.sh
```

Expected: `Result: Succeeded`, then `passed: N` with no `FAILED`. Record N: every later full-suite run must show at least N plus this slice's new tests.

---

## Task 2 (D1): The pure core -- bays, ratings, the stock ship as numbers, the plain state

**Owner:** D. **Depends on:** Task 1. Spec decisions 1, 2, 3, 4, 6, 11.

**Files:**
- Create: `Source/DeepSpace/Ship/ShipParts.h`, `Source/DeepSpace/Ship/ShipParts.cpp`
- Create: `Source/DeepSpace/Tests/ShipPartsTest.cpp` (`DeepSpace.Ship.Parts.Arithmetic`)

**Interfaces:**
- Consumes: `FShipFlightState::JumpChargeSeconds` (`ShipFlightState.h:319`), `ShipDriveLever::DefaultResponse` (`ShipDriveLever.h:63`) and `FShipFlightLimits::Cruise()` (`ShipFlightState.h:71`), all in the test only.
- Produces: everything under `ShipParts.h` in *Interfaces at the seams*, except the D3 lines.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/ShipPartsTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipDriveLever.h"
#include "Ship/ShipFlightState.h"
#include "Ship/ShipParts.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * The wear and upgrades spec's pure core: the bays, the ratings, the stock
 * ship as numbers (ruling 1: today's ship, bit for bit), and the one rule for
 * a console variable over a fitted part (decision 6).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsArithmeticTest, "DeepSpace.Ship.Parts.Arithmetic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsArithmeticTest::RunTest(const FString& Parameters)
{
    // -- the stock ship is today's ship ----------------------------------------
    const FShipRatings Stock = FShipRatings::Stock();
    TestEqual(TEXT("the stock reactor makes 1400 W"), Stock.ReactorWatts, 1400.0);
    TestEqual(TEXT("the lights want 300 W"), Stock.LightsWant, 300.0);
    TestEqual(TEXT("the boosters want 450 W"), Stock.BoostersWant, 450.0);
    TestEqual(TEXT("and push cruise's own 2 km/s^2"), Stock.LinearAcceleration, FShipFlightLimits::Cruise().LinearAcceleration);
    TestEqual(TEXT("the jump winds on 380 W"), Stock.WindingWant, 380.0);
    TestEqual(TEXT("in the settled 45 s"), Stock.ChargeSeconds, FShipFlightState::JumpChargeSeconds);
    TestEqual(TEXT("the drive follows its lever at 3 notches a second"), Stock.DriveResponse, ShipDriveLever::DefaultResponse);
    TestEqual(TEXT("and the chart reaches 12 ly"), Stock.RangeLy, 12.0);

    // -- every rating reads back, has a name, and has one owner ----------------
    TestEqual(TEXT("eight ratings"), ShipParts::AllRatings().Num(), 8);
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        const FString Name = ShipParts::RatingName(Rating).ToString();
        FShipRatings Moved = Stock;
        Moved.Set(Rating, 1234.5);
        TestEqual(FString::Printf(TEXT("%s reads back what was set"), *Name), Moved.Get(Rating), 1234.5);
        TestTrue(FString::Printf(TEXT("%s round-trips through its name"), *Name),
                 ShipParts::RatingFromName(Name) == TOptional<EShipRating>(Rating));
        TestTrue(FString::Printf(TEXT("%s is owned by a core bay"), *Name), ShipBay::IsCore(ShipParts::OwnerOf(Rating)));
    }
    TestFalse(TEXT("a name no rating has is none"), ShipParts::RatingFromName(TEXT("TopSpeed")).IsSet());
    TestTrue(TEXT("the drive owns its response"), ShipParts::OwnerOf(EShipRating::DriveResponse) == EShipBay::Drive);
    TestTrue(TEXT("the sensors own the range"), ShipParts::OwnerOf(EShipRating::RangeLy) == EShipBay::Sensors);

    // -- a part's ratings over stock; a rating it lacks reads stock ------------
    FShipPartSpec TwinCore;
    TwinCore.Id = TEXT("Reactor.TwinCore");
    TwinCore.Bay = EShipBay::Reactor;
    TwinCore.Ratings.Add(EShipRating::ReactorWatts, 1800.0);
    const FShipRatings Fitted = ShipParts::RatingsOf({ TwinCore });
    TestEqual(TEXT("the twin core rates 1800 W"), Fitted.ReactorWatts, 1800.0);
    TestEqual(TEXT("and leaves the lights' want at stock, never zero"), Fitted.LightsWant, Stock.LightsWant);
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        TestEqual(FString::Printf(TEXT("no parts reads stock %s"), *ShipParts::RatingName(Rating).ToString()),
                  ShipParts::RatingsOf({}).Get(Rating), Stock.Get(Rating));
    }

    // -- a CVar over a part (decision 6; review focus 1) -----------------------
    TestEqual(TEXT("-1, the default, reads the part"), ShipParts::Effective(12.0, -1.0f), 12.0);
    TestEqual(TEXT("15 reads 15"), ShipParts::Effective(12.0, 15.0f), 15.0);
    TestEqual(TEXT("0 is an override too: the tests' instant charge"), ShipParts::Effective(45.0, 0.0f), 0.0);
    TestEqual(TEXT("any negative value reads the part, never itself"), ShipParts::Effective(12.0, -5.0f), 12.0);

    // -- the bays ---------------------------------------------------------------
    TestEqual(TEXT("six core bays and two auxiliary slots"), ShipBay::All().Num(), 8);
    TestFalse(TEXT("None is not a bay"), ShipBay::All().Contains(EShipBay::None));
    for (const EShipBay Bay : ShipBay::All())
    {
        const FName Name = ShipBay::Name(Bay);
        TestTrue(FString::Printf(TEXT("%s round-trips through its name"), *Name.ToString()),
                 ShipBay::FromName(Name) == TOptional<EShipBay>(Bay));
        TestTrue(FString::Printf(TEXT("%s is core or aux, never both"), *Name.ToString()), ShipBay::IsCore(Bay) != ShipBay::IsAux(Bay));
    }
    TestFalse(TEXT("a bay the ship has not got is none"), ShipBay::FromName(TEXT("Galley")).IsSet());
    TestFalse(TEXT("and neither is None"), ShipBay::FromName(NAME_None).IsSet());
    TestFalse(TEXT("None is neither core nor aux"), ShipBay::IsCore(EShipBay::None) || ShipBay::IsAux(EShipBay::None));
    TestEqual(TEXT("draws are booked by bay"), ShipBay::DrawKey(EShipBay::Lights), FName(TEXT("Bay.Lights")));
    TestEqual(TEXT("a core bay's stock part is <Bay>.Stock"), ShipBay::StockPartId(EShipBay::Reactor), FName(TEXT("Reactor.Stock")));
    TestEqual(TEXT("life support's too"), ShipBay::StockPartId(EShipBay::LifeSupport), FName(TEXT("LifeSupport.Stock")));
    TestEqual(TEXT("an aux slot has none"), ShipBay::StockPartId(EShipBay::Aux1), FName(NAME_None));
    TestEqual(TEXT("the sensors' plate says NAV"), ShipBay::PlateLabel(EShipBay::Sensors), FString(TEXT("NAV")));
    TestEqual(TEXT("an aux plate says AUX"), ShipBay::PlateLabel(EShipBay::Aux2), FString(TEXT("AUX")));

    // -- the plain state: one entry per bay, found by name ---------------------
    FShipLoadoutState Empty = ShipParts::EmptyLoadout();
    TestEqual(TEXT("an empty loadout lists every bay"), Empty.Bays.Num(), ShipBay::All().Num());
    TestEqual(TEXT("and no spares"), Empty.Spares.Num(), 0);
    for (const EShipBay Bay : ShipBay::All())
    {
        const FShipBayState* Entry = ShipParts::FindBay(Empty, Bay);
        TestTrue(FString::Printf(TEXT("%s is found by name and holds nothing"), *ShipBay::Name(Bay).ToString()),
                 Entry && Entry->Bay == ShipBay::Name(Bay) && Entry->Part.PartId.IsNone() && Entry->LivesDrawn == 0);
    }
    TestNull(TEXT("None is not in it"), ShipParts::FindBay(Empty, EShipBay::None));
    return true;
}

#endif
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh
```

Expected: the build fails, `'Ship/ShipParts.h' file not found`.

- [ ] **Step 3: Write the header**

Create `Source/DeepSpace/Ship/ShipParts.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "ShipParts.generated.h"

/**
 * The ship's bays (wear and upgrades decision 1): six fixed, one per thing
 * the ship already models, and two auxiliary slots. Each holds one part.
 * Fixed bays with one part each leave nothing to pack, so no loadout is a
 * knapsack with an optimum to converge on.
 *
 * None is first on purpose. A part whose Bay was never set (made with
 * NewObject in a test, or an asset a script missed) reads None, and
 * UShipSubsystem::FitPart refuses it rather than defaulting it into the
 * reactor bay and displacing the reactor. Bays are saved by name
 * (ShipBay::Name), never by position, so adding one later shifts nothing.
 */
UENUM(BlueprintType)
enum class EShipBay : uint8
{
    None,
    Reactor,
    Drive,
    Boosters,
    Lights,
    LifeSupport,
    Sensors,
    Aux1,
    Aux2,
};

/** A rated value a part can set. Each is owned by exactly one bay
 *  (decision 2's table; ShipParts::OwnerOf). No rating is a top speed. */
UENUM(BlueprintType)
enum class EShipRating : uint8
{
    ReactorWatts,
    WindingWant,
    ChargeSeconds,
    DriveResponse,
    BoostersWant,
    LinearAcceleration,
    LightsWant,
    RangeLy,
};

/** One part as the pure rules see it, with no UObject:
 *  UShipModuleDataAsset::GetSpec builds it, and the tests build it from
 *  Tools/ship_parts.json. */
struct DEEPSPACE_API FShipPartSpec
{
    FName Id;
    EShipBay Bay = EShipBay::None;

    /** Watts off the top while fitted. */
    double Draw = 0.0;

    /** Only the ratings this part sets; any other reads stock. */
    TMap<EShipRating, double> Ratings;
};

/** Every rated value the ship runs on, derived and never stored. */
struct DEEPSPACE_API FShipRatings
{
    double ReactorWatts = 0.0;
    double WindingWant = 0.0;
    double ChargeSeconds = 0.0;
    double DriveResponse = 0.0;
    double BoostersWant = 0.0;
    double LinearAcceleration = 0.0;
    double LightsWant = 0.0;
    double RangeLy = 0.0;

    /** Today's ship (ruling 1), and what an empty core bay reads (decision 3). */
    static FShipRatings Stock();

    double Get(EShipRating Rating) const;
    void Set(EShipRating Rating, double Value);
};

/**
 * One particular part, fitted or spare (decision 11): a plain value the
 * struct serialiser can write field by field and a test can compare. Ids,
 * never pointers, so a part that no longer exists can be reported and fall
 * back to stock. The wear fields exist from slice 1 and stay zero until
 * slice 4, so the save (slice 3) needs no migration when wear arrives.
 */
USTRUCT()
struct DEEPSPACE_API FShipPartState
{
    GENERATED_BODY()

    /** Which part. None in a bay is an empty bay: it reads the stock part's
     *  ratings and draws nothing (decision 3). */
    UPROPERTY()
    FName PartId;

    /** Slice 4: jumps this part has aged. */
    UPROPERTY()
    double AgeJumps = 0.0;

    /** Slice 4: the age at which its symptom arrives. */
    UPROPERTY()
    double LifeJumps = 0.0;

    /** Slice 4: false until first fitted (decision 10). */
    UPROPERTY()
    bool bHasLife = false;

    /** Slice 4: None, or one of the bay's symptoms. */
    UPROPERTY()
    FName Symptom;

    /** Slice 4: history for its words and its look. */
    UPROPERTY()
    int32 Repairs = 0;

    /** Slice 4: one of the ship's seeded stock parts (decision 19). */
    UPROPERTY()
    bool bOriginal = false;
};

/** One bay: its name, never its position, and the part in it. */
USTRUCT()
struct DEEPSPACE_API FShipBayState
{
    GENERATED_BODY()

    /** ShipBay::Name of the bay. */
    UPROPERTY()
    FName Bay;

    UPROPERTY()
    FShipPartState Part;

    /** Slice 4: lives this bay has drawn, for the wear seed. */
    UPROPERTY()
    int32 LivesDrawn = 0;
};

/** Everything aboard that is a part: the one truth UShipSubsystem keeps. */
USTRUCT()
struct DEEPSPACE_API FShipLoadoutState
{
    GENERATED_BODY()

    /** One per bay, found by name (ShipParts::FindBay). */
    UPROPERTY()
    TArray<FShipBayState> Bays;

    /** Each keeps its own state (decision 10). No carry limit, no weight:
     *  a spare is not an object to manage. */
    UPROPERTY()
    TArray<FShipPartState> Spares;
};

namespace ShipBay
{
    /** "Reactor", "LifeSupport", "Aux1": what a bay is saved and found by.
     *  NAME_None for None. */
    DEEPSPACE_API FName Name(EShipBay Bay);

    /** The bay a name names; unset for None and for a name no bay has. */
    DEEPSPACE_API TOptional<EShipBay> FromName(FName BayName);

    /** The six fixed bays. */
    DEEPSPACE_API bool IsCore(EShipBay Bay);

    /** Aux1 and Aux2. */
    DEEPSPACE_API bool IsAux(EShipBay Bay);

    /** Every bay, in bay order (the nameplates' order): the six core bays,
     *  then the two auxiliary slots. Never None. */
    DEEPSPACE_API const TArray<EShipBay>& All();

    /** The key a bay's draw is booked under in FShipPowerState, "Bay.Lights"
     *  (decision 4). A swap is RemoveDraw and AddDraw under one key, so two
     *  parts in one bay can never both draw. */
    DEEPSPACE_API FName DrawKey(EShipBay Bay);

    /** "Reactor.Stock": the part a core bay starts with. None for an aux slot. */
    DEEPSPACE_API FName StockPartId(EShipBay Bay);

    /** The nameplate's first column: "REACTOR", "LIFE SUPPORT", "NAV", "AUX". */
    DEEPSPACE_API FString PlateLabel(EShipBay Bay);
}

namespace ShipParts
{
    /**
     * The stock ship's numbers, today's exactly (ruling 1). Moved here from
     * UShipSubsystem's constants and the four CVars' old defaults; the
     * catalogue's stock rows (Tools/ship_parts.json) carry the same numbers,
     * and DeepSpace.Ship.Parts.Contract holds them equal.
     *
     * The reactor is sized so the stock ship is whole at rest: its draws (620
     * W) plus the lights (300) and the boosters (450) come to 1370 W, so at
     * the default split nothing is dimmed while nothing is being asked of the
     * ship. The split bites when the jump winds -- the first playtest found
     * the lights at 63% on a quiet ship under the old 1000 W, which read as
     * broken rather than strained (developer's ruling, 2026-09-26).
     * DeepSpace.Ship.JumpCanWindAtFullSpeed holds it.
     *
     * The wants sum to more than the reactor makes, deliberately: if
     * everything could be fed at once the split would never be a choice, and
     * a choice with no cost is not one. The engine has no want at rest: it
     * asks WindingWant only while the jump winds, so an idle drive costs the
     * ship nothing (nav decision 4). An idle want of zero reads as full
     * satisfaction, so anything that follows the winding -- the hum -- reads
     * watts delivered over WindingWant, never satisfaction (plan conflict 8).
     */
    inline constexpr double StockReactorWatts = 1400.0;
    inline constexpr double StockLightsWant = 300.0;
    inline constexpr double StockBoostersWant = 450.0;

    /** Cruise's own 2 km/s^2 at full feed (FShipFlightLimits::Cruise). */
    inline constexpr double StockLinearAcceleration = 2.0e5;

    /** 380 W: small enough that an engine-first split winds at full speed on
     *  the stock hauler, which has 780 W left once its draws are off the top.
     *  At the old 800 W against a 1000 W reactor the best any split reached
     *  was 48% fed, and the charge time was a number no player could see.
     *  Settled 2026-09-26. */
    inline constexpr double StockWindingWant = 380.0;

    /** Settled 2026-09-26; FShipFlightState::JumpChargeSeconds is the same. */
    inline constexpr double StockChargeSeconds = 45.0;

    /** ShipDriveLever::DefaultResponse. */
    inline constexpr double StockDriveResponse = 3.0;

    inline constexpr double StockRangeLy = 12.0;

    /** "ReactorWatts": the key Tools/ship_parts.json rates by. */
    DEEPSPACE_API FName RatingName(EShipRating Rating);

    /** Case-sensitive, as the JSON is; unset for a name no rating has. */
    DEEPSPACE_API TOptional<EShipRating> RatingFromName(const FString& Text);

    DEEPSPACE_API const TArray<EShipRating>& AllRatings();

    /** The one bay that may rate Rating (decision 2's table). */
    DEEPSPACE_API EShipBay OwnerOf(EShipRating Rating);

    /** Each of Rated's values over Into's. */
    DEEPSPACE_API void Apply(FShipRatings& Into, const TMap<EShipRating, double>& Rated);

    /** Stock, with every fitted part's ratings applied over it. */
    DEEPSPACE_API FShipRatings RatingsOf(const TArray<FShipPartSpec>& Fitted);

    /**
     * A playtest CVar over a fitted part's rating (decision 6): the CVar when
     * it is 0 or more, else the rating. -1 is the default. Zero overrides --
     * ds.Nav.ChargeSeconds 0 is how tests make a jump instant -- and any
     * negative value, not only -1, reads the part.
     */
    DEEPSPACE_API double Effective(double Rated, float CVar);

    /** Every bay, in ShipBay::All's order, empty. */
    DEEPSPACE_API FShipLoadoutState EmptyLoadout();

    /** The entry for Bay, found by name; null if State has none. */
    DEEPSPACE_API FShipBayState* FindBay(FShipLoadoutState& State, EShipBay Bay);
    DEEPSPACE_API const FShipBayState* FindBay(const FShipLoadoutState& State, EShipBay Bay);
}
```

- [ ] **Step 4: Write the implementation**

Create `Source/DeepSpace/Ship/ShipParts.cpp`:

```cpp
#include "Ship/ShipParts.h"

FShipRatings FShipRatings::Stock()
{
    FShipRatings Ratings;
    Ratings.ReactorWatts = ShipParts::StockReactorWatts;
    Ratings.WindingWant = ShipParts::StockWindingWant;
    Ratings.ChargeSeconds = ShipParts::StockChargeSeconds;
    Ratings.DriveResponse = ShipParts::StockDriveResponse;
    Ratings.BoostersWant = ShipParts::StockBoostersWant;
    Ratings.LinearAcceleration = ShipParts::StockLinearAcceleration;
    Ratings.LightsWant = ShipParts::StockLightsWant;
    Ratings.RangeLy = ShipParts::StockRangeLy;
    return Ratings;
}

double FShipRatings::Get(EShipRating Rating) const
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       return ReactorWatts;
    case EShipRating::WindingWant:        return WindingWant;
    case EShipRating::ChargeSeconds:      return ChargeSeconds;
    case EShipRating::DriveResponse:      return DriveResponse;
    case EShipRating::BoostersWant:       return BoostersWant;
    case EShipRating::LinearAcceleration: return LinearAcceleration;
    case EShipRating::LightsWant:         return LightsWant;
    case EShipRating::RangeLy:            return RangeLy;
    }
    return 0.0;
}

void FShipRatings::Set(EShipRating Rating, double Value)
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       ReactorWatts = Value; break;
    case EShipRating::WindingWant:        WindingWant = Value; break;
    case EShipRating::ChargeSeconds:      ChargeSeconds = Value; break;
    case EShipRating::DriveResponse:      DriveResponse = Value; break;
    case EShipRating::BoostersWant:       BoostersWant = Value; break;
    case EShipRating::LinearAcceleration: LinearAcceleration = Value; break;
    case EShipRating::LightsWant:         LightsWant = Value; break;
    case EShipRating::RangeLy:            RangeLy = Value; break;
    }
}

FName ShipBay::Name(EShipBay Bay)
{
    switch (Bay)
    {
    case EShipBay::Reactor:     return FName(TEXT("Reactor"));
    case EShipBay::Drive:       return FName(TEXT("Drive"));
    case EShipBay::Boosters:    return FName(TEXT("Boosters"));
    case EShipBay::Lights:      return FName(TEXT("Lights"));
    case EShipBay::LifeSupport: return FName(TEXT("LifeSupport"));
    case EShipBay::Sensors:     return FName(TEXT("Sensors"));
    case EShipBay::Aux1:        return FName(TEXT("Aux1"));
    case EShipBay::Aux2:        return FName(TEXT("Aux2"));
    case EShipBay::None:        break;
    }
    return NAME_None;
}

TOptional<EShipBay> ShipBay::FromName(FName BayName)
{
    for (const EShipBay Bay : All())
    {
        if (Name(Bay) == BayName)
        {
            return Bay;
        }
    }
    return {};
}

bool ShipBay::IsAux(EShipBay Bay)
{
    return Bay == EShipBay::Aux1 || Bay == EShipBay::Aux2;
}

bool ShipBay::IsCore(EShipBay Bay)
{
    return Bay != EShipBay::None && !IsAux(Bay);
}

const TArray<EShipBay>& ShipBay::All()
{
    static const TArray<EShipBay> Bays = {
        EShipBay::Reactor, EShipBay::Drive, EShipBay::Boosters, EShipBay::Lights,
        EShipBay::LifeSupport, EShipBay::Sensors, EShipBay::Aux1, EShipBay::Aux2,
    };
    return Bays;
}

FName ShipBay::DrawKey(EShipBay Bay)
{
    return FName(*(FString(TEXT("Bay.")) + Name(Bay).ToString()));
}

FName ShipBay::StockPartId(EShipBay Bay)
{
    return IsCore(Bay) ? FName(*(Name(Bay).ToString() + TEXT(".Stock"))) : NAME_None;
}

FString ShipBay::PlateLabel(EShipBay Bay)
{
    switch (Bay)
    {
    case EShipBay::Reactor:     return TEXT("REACTOR");
    case EShipBay::Drive:       return TEXT("DRIVE");
    case EShipBay::Boosters:    return TEXT("BOOSTERS");
    case EShipBay::Lights:      return TEXT("LIGHTS");
    case EShipBay::LifeSupport: return TEXT("LIFE SUPPORT");
    case EShipBay::Sensors:     return TEXT("NAV");
    case EShipBay::Aux1:
    case EShipBay::Aux2:        return TEXT("AUX");
    case EShipBay::None:        break;
    }
    return FString();
}

FName ShipParts::RatingName(EShipRating Rating)
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       return FName(TEXT("ReactorWatts"));
    case EShipRating::WindingWant:        return FName(TEXT("WindingWant"));
    case EShipRating::ChargeSeconds:      return FName(TEXT("ChargeSeconds"));
    case EShipRating::DriveResponse:      return FName(TEXT("DriveResponse"));
    case EShipRating::BoostersWant:       return FName(TEXT("BoostersWant"));
    case EShipRating::LinearAcceleration: return FName(TEXT("LinearAcceleration"));
    case EShipRating::LightsWant:         return FName(TEXT("LightsWant"));
    case EShipRating::RangeLy:            return FName(TEXT("RangeLy"));
    }
    return NAME_None;
}

TOptional<EShipRating> ShipParts::RatingFromName(const FString& Text)
{
    for (const EShipRating Rating : AllRatings())
    {
        if (Text.Equals(RatingName(Rating).ToString(), ESearchCase::CaseSensitive))
        {
            return Rating;
        }
    }
    return {};
}

const TArray<EShipRating>& ShipParts::AllRatings()
{
    static const TArray<EShipRating> Ratings = {
        EShipRating::ReactorWatts, EShipRating::WindingWant, EShipRating::ChargeSeconds, EShipRating::DriveResponse,
        EShipRating::BoostersWant, EShipRating::LinearAcceleration, EShipRating::LightsWant, EShipRating::RangeLy,
    };
    return Ratings;
}

EShipBay ShipParts::OwnerOf(EShipRating Rating)
{
    switch (Rating)
    {
    case EShipRating::ReactorWatts:       return EShipBay::Reactor;
    case EShipRating::WindingWant:
    case EShipRating::ChargeSeconds:
    case EShipRating::DriveResponse:      return EShipBay::Drive;
    case EShipRating::BoostersWant:
    case EShipRating::LinearAcceleration: return EShipBay::Boosters;
    case EShipRating::LightsWant:         return EShipBay::Lights;
    case EShipRating::RangeLy:            return EShipBay::Sensors;
    }
    return EShipBay::None;
}

void ShipParts::Apply(FShipRatings& Into, const TMap<EShipRating, double>& Rated)
{
    for (const TPair<EShipRating, double>& Rating : Rated)
    {
        Into.Set(Rating.Key, Rating.Value);
    }
}

FShipRatings ShipParts::RatingsOf(const TArray<FShipPartSpec>& Fitted)
{
    FShipRatings Out = FShipRatings::Stock();
    for (const FShipPartSpec& Part : Fitted)
    {
        Apply(Out, Part.Ratings);
    }
    return Out;
}

double ShipParts::Effective(double Rated, float CVar)
{
    return CVar >= 0.0f ? static_cast<double>(CVar) : Rated;
}

FShipLoadoutState ShipParts::EmptyLoadout()
{
    FShipLoadoutState State;
    for (const EShipBay Bay : ShipBay::All())
    {
        FShipBayState Entry;
        Entry.Bay = ShipBay::Name(Bay);
        State.Bays.Add(Entry);
    }
    return State;
}

FShipBayState* ShipParts::FindBay(FShipLoadoutState& State, EShipBay Bay)
{
    const FName Wanted = ShipBay::Name(Bay);
    return Wanted.IsNone() ? nullptr
        : State.Bays.FindByPredicate([Wanted](const FShipBayState& Entry) { return Entry.Bay == Wanted; });
}

const FShipBayState* ShipParts::FindBay(const FShipLoadoutState& State, EShipBay Bay)
{
    const FName Wanted = ShipBay::Name(Bay);
    return Wanted.IsNone() ? nullptr
        : State.Bays.FindByPredicate([Wanted](const FShipBayState& Entry) { return Entry.Bay == Wanted; });
}
```

- [ ] **Step 5: Run it and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh && ./test.sh DeepSpace.Ship.Parts.Arithmetic
```

Expected: `Result: Succeeded`, then `passed: 1`.

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
git add Source/DeepSpace/Ship/ShipParts.h Source/DeepSpace/Ship/ShipParts.cpp Source/DeepSpace/Tests/ShipPartsTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(parts): the pure core -- bays, ratings, the stock ship as numbers, the plain loadout state

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it can fail (review focus 1)**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipParts.cpp 'return CVar >= 0.0f ? static_cast<double>(CVar) : Rated;' 'return CVar > 0.0f ? static_cast<double>(CVar) : Rated;' DeepSpace.Ship.Parts.Arithmetic && \
./build.sh
```

Expected: `KILLED` ("0 is an override too" fails), then `Result: Succeeded`.

---

## Task 3 (D2): A module is a part -- its bay, words and ratings; the catalogue asset class

**Owner:** D. **Depends on:** Task 2. Spec decisions 1, 5, 9.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipModuleDataAsset.h` (whole file, 33 lines), `Source/DeepSpace/Ship/ShipModuleDataAsset.cpp` (whole file, 3 lines)
- Create: `Source/DeepSpace/Ship/ShipPartCatalogue.h`
- Modify: `Source/DeepSpace/Tests/ShipPartsTest.cpp` (append `DeepSpace.Ship.Parts.AssetSpec`)

**Interfaces:**
- Consumes: `EShipBay`, `EShipRating`, `FShipPartSpec` (D1).
- Produces: `UShipModuleDataAsset::Bay`, `::Words`, `::Ratings`, `FShipPartSpec UShipModuleDataAsset::GetSpec() const`; `UShipPartCatalogue::Parts`.

- [ ] **Step 1: Write the failing test**

Append to `Source/DeepSpace/Tests/ShipPartsTest.cpp`, before the closing `#endif`, and add `#include "Ship/ShipModuleDataAsset.h"` and `#include "Ship/ShipPartCatalogue.h"` to its includes:

```cpp
/*
 * A module is a part (decision 1): it says which bay it fits, and a part
 * whose bay was never set reads None rather than the reactor.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsAssetSpecTest, "DeepSpace.Ship.Parts.AssetSpec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsAssetSpecTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("a part whose bay was never set reads None, never the reactor"),
             GetDefault<UShipModuleDataAsset>()->Bay == EShipBay::None);

    UShipModuleDataAsset* Part = NewObject<UShipModuleDataAsset>();
    Part->ModuleId = TEXT("Drive.QuickLever");
    Part->Bay = EShipBay::Drive;
    Part->PowerDraw = 25.0f;
    Part->Words = FText::FromString(TEXT("Takes the lever before you have let go of it."));
    Part->Ratings.Add(EShipRating::DriveResponse, 4.5);

    const FShipPartSpec Spec = Part->GetSpec();
    TestEqual(TEXT("the spec carries the id"), Spec.Id, FName(TEXT("Drive.QuickLever")));
    TestTrue(TEXT("and the bay"), Spec.Bay == EShipBay::Drive);
    TestEqual(TEXT("and the draw"), Spec.Draw, 25.0);
    TestTrue(TEXT("and exactly the ratings it sets"),
             Spec.Ratings.Num() == 1 && Spec.Ratings.Contains(EShipRating::DriveResponse) && Spec.Ratings[EShipRating::DriveResponse] == 4.5);

    UShipPartCatalogue* Catalogue = NewObject<UShipPartCatalogue>();
    Catalogue->Parts.Add(Part);
    TestTrue(TEXT("a catalogue holds its parts by soft pointer"), Catalogue->Parts.Num() == 1 && Catalogue->Parts[0].Get() == Part);
    return true;
}
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh
```

Expected: the build fails, `'Ship/ShipPartCatalogue.h' file not found` (and `no member named 'Bay' in 'UShipModuleDataAsset'`).

- [ ] **Step 3: The part's fields**

Replace `Source/DeepSpace/Ship/ShipModuleDataAsset.h` with:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Ship/ShipParts.h"
#include "ShipModuleDataAsset.generated.h"

/**
 * One part: a module in a bay (wear and upgrades decision 1). "Module" and
 * "part" name the same thing. The class keeps its name because renaming a
 * UCLASS strands every asset saved against the old name.
 *
 * Data, never logic (ADR 0002), authored from Tools/ship_parts.json by
 * Tools/setup_ship_parts.py. Never edit one by hand: a number settled into a
 * .uasset is invisible to git, and DeepSpace.Ship.Parts.Contract holds the
 * assets to the JSON.
 */
UCLASS(BlueprintType)
class DEEPSPACE_API UShipModuleDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** `<Bay>.<Name>` (Reactor.TwinCore): the catalogue's key, and what the
     *  loadout state saves. Draws are booked by bay, never by this
     *  (decision 4). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FName ModuleId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FText DisplayName;

    /** Watts off the top while fitted, before any split. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    float PowerDraw = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    TSoftObjectPtr<UStaticMesh> Mesh;

    /** The bay it fits. None until set, and UShipSubsystem::FitPart refuses
     *  a None part rather than defaulting it into the reactor bay. An aux
     *  part says Aux1, and fits either auxiliary slot. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    EShipBay Bay = EShipBay::None;

    /** Its nameplate's words: character and history, never quality
     *  (decision 9). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FText Words;

    /** The rated values it sets, each one its own bay owns (decision 2). A
     *  rating it lacks reads the stock part's, never zero (decision 3). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    TMap<EShipRating, double> Ratings;

    /** The part as the pure rules see it (ShipParts::Validate). */
    FShipPartSpec GetSpec() const;
};
```

Replace `Source/DeepSpace/Ship/ShipModuleDataAsset.cpp` with:

```cpp
#include "Ship/ShipModuleDataAsset.h"

FShipPartSpec UShipModuleDataAsset::GetSpec() const
{
    FShipPartSpec Spec;
    Spec.Id = ModuleId;
    Spec.Bay = Bay;
    Spec.Draw = PowerDraw;
    Spec.Ratings = Ratings;
    return Spec;
}
```

- [ ] **Step 4: The catalogue's class**

Create `Source/DeepSpace/Ship/ShipPartCatalogue.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShipPartCatalogue.generated.h"

class UShipModuleDataAsset;

/**
 * Every part there is, in Tools/ship_parts.json's order (wear and upgrades
 * decision 5). UShipSubsystem finds a part by id through this one list,
 * named by its CatalogueAsset ini line. That needs no asset-registry scan,
 * so it works the same under -nullrhi and in a bare test world, with no
 * game mode.
 *
 * Written by Tools/setup_ship_parts.py; never edit by hand.
 */
UCLASS(BlueprintType)
class DEEPSPACE_API UShipPartCatalogue : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Parts")
    TArray<TSoftObjectPtr<UShipModuleDataAsset>> Parts;
};
```

- [ ] **Step 5: Run it and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh && ./test.sh DeepSpace.Ship.Parts.AssetSpec
```

Expected: `Result: Succeeded`, then `passed: 1`.

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
git add Source/DeepSpace/Ship/ShipModuleDataAsset.h Source/DeepSpace/Ship/ShipModuleDataAsset.cpp \
        Source/DeepSpace/Ship/ShipPartCatalogue.h Source/DeepSpace/Tests/ShipPartsTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(parts): a module is a part -- its bay, words and ratings; UShipPartCatalogue

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipModuleDataAsset.cpp 'Spec.Draw = PowerDraw;' 'Spec.Draw = 0.0;' DeepSpace.Ship.Parts.AssetSpec && \
./build.sh
```

Expected: `KILLED`, then `Result: Succeeded`.

---

## Task 4 (D3): The catalogue JSON and decision 7's rules over it

**Owner:** D. **Depends on:** Task 3. Spec decisions 2, 5, 7, 8; sign-offs 3, 4, 5, 24.

**Files:**
- Create: `Tools/ship_parts.json`
- Create: `Source/DeepSpace/Tests/ShipPartsJson.h`
- Modify: `Source/DeepSpace/Ship/ShipParts.h` (append to `namespace ShipParts`), `Source/DeepSpace/Ship/ShipParts.cpp` (append)
- Modify: `Source/DeepSpace/Tests/ShipPartsTest.cpp` (append `DeepSpace.Ship.Parts.CatalogueRules`)

**Interfaces:**
- Consumes: D1's core.
- Produces: `ShipParts::FAxis`, `AxesOf`, `Openness`, `AtRestWatts`, `Validate`; `ShipPartsJson::*` (used by S's tests too); the JSON's shape, which the script reads:
  - top level: `directory`, `catalogue`, `parts`;
  - each row: `id`, `asset`, `bay`, `name`, `words`, `draw`, `ratings` (keys are `ShipParts::RatingName`s).

- [ ] **Step 1: The catalogue**

Create `Tools/ship_parts.json`:

```json
{
  "_comment": "The ship's parts (wear and upgrades decision 5): the one source. Tools/setup_ship_parts.py authors Content/Ship/Parts from it; DeepSpace.Ship.Parts.Contract holds the assets and FShipRatings::Stock() to it; DeepSpace.Ship.Parts.CatalogueRules holds decision 7 over it. Ratings are in engine units (LinearAcceleration in cm/s^2). A rating a row leaves out reads stock. Names and words are placeholders (sign-off 12): character and history, never quality.",
  "directory": "/Game/Ship/Parts",
  "catalogue": "DA_ShipCatalogue",
  "parts": [
    {"id": "Reactor.Stock", "asset": "DA_Reactor_Stock", "bay": "Reactor", "name": "Fusion bottle",
     "words": "Hums in the deck plates. Nobody aboard remembers it new.", "draw": 0,
     "ratings": {"ReactorWatts": 1400}},
    {"id": "Reactor.TwinCore", "asset": "DA_Reactor_TwinCore", "bay": "Reactor", "name": "Twin-core reactor",
     "words": "Hums a fifth low. Came off a salvage tug.", "draw": 0,
     "ratings": {"ReactorWatts": 1800}},
    {"id": "Drive.Stock", "asset": "DA_Drive_Stock", "bay": "Drive", "name": "Fold drive",
     "words": "Ticks for a minute after every fold.", "draw": 0,
     "ratings": {"WindingWant": 380, "ChargeSeconds": 45, "DriveResponse": 3}},
    {"id": "Drive.QuickLever", "asset": "DA_Drive_QuickLever", "bay": "Drive", "name": "Quick-lever drive",
     "words": "Takes the lever before you have let go of it.", "draw": 0,
     "ratings": {"WindingWant": 380, "ChargeSeconds": 45, "DriveResponse": 4.5}},
    {"id": "Boosters.Stock", "asset": "DA_Boosters_Stock", "bay": "Boosters", "name": "Booster cluster",
     "words": "Quiet until you lean on them.", "draw": 0,
     "ratings": {"BoostersWant": 450, "LinearAcceleration": 200000}},
    {"id": "Lights.Stock", "asset": "DA_Lights_Stock", "bay": "Lights", "name": "Interior lighting",
     "words": "Warm, a little amber in the corridor.", "draw": 120,
     "ratings": {"LightsWant": 300}},
    {"id": "LifeSupport.Stock", "asset": "DA_LifeSupport_Stock", "bay": "LifeSupport", "name": "Air plant",
     "words": "Somebody taped the filter door shut.", "draw": 300,
     "ratings": {}},
    {"id": "Sensors.Stock", "asset": "DA_Sensors_Stock", "bay": "Sensors", "name": "Survey array",
     "words": "Older than the hull, and it shows.", "draw": 200,
     "ratings": {"RangeLy": 12}}
  ]
}
```

The three stock draws (lights 120, life support 300, sensors 200) are the hand-made modules' draws, which the milestone 1 plan measured and which sum to 620 W. Task 5 (D4) reads them from the assets before it writes anything. If they differ, it stops and prints what the assets say, and those numbers go here.

- [ ] **Step 2: The JSON reader the tests share**

Create `Source/DeepSpace/Tests/ShipPartsJson.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Ship/ShipParts.h"

// Tools/ship_parts.json, the catalogue's one source (wear and upgrades
// decision 5), read the way Tools/setup_ship_parts.py reads it. Anything the
// file says that the C++ cannot read -- a bay or a rating with no enumerator
// -- is a problem, never skipped.
namespace ShipPartsJson
{
    struct FRow
    {
        FShipPartSpec Spec;
        FString Asset;
        FString Name;
        FString Words;
    };

    struct FCatalogue
    {
        FString Directory;
        FString CatalogueAsset;
        TArray<FRow> Rows;
        TArray<FString> Problems;
    };

    inline FString Path()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/ship_parts.json"));
    }

    inline FCatalogue Read()
    {
        FCatalogue Out;
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path()))
        {
            Out.Problems.Add(TEXT("cannot read ") + Path());
            return Out;
        }
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            Out.Problems.Add(TEXT("not JSON: ") + Path());
            return Out;
        }
        Out.Directory = Root->GetStringField(TEXT("directory"));
        Out.CatalogueAsset = Root->GetStringField(TEXT("catalogue"));
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("parts")))
        {
            const TSharedPtr<FJsonObject> Part = Value->AsObject();
            FRow Row;
            Row.Spec.Id = FName(*Part->GetStringField(TEXT("id")));
            const FString BayText = Part->GetStringField(TEXT("bay"));
            const TOptional<EShipBay> Bay = ShipBay::FromName(FName(*BayText));
            if (!Bay)
            {
                Out.Problems.Add(FString::Printf(TEXT("%s: no bay is called %s"), *Row.Spec.Id.ToString(), *BayText));
            }
            Row.Spec.Bay = Bay.Get(EShipBay::None);
            Row.Spec.Draw = Part->GetNumberField(TEXT("draw"));
            for (const TPair<FString, TSharedPtr<FJsonValue>>& Rated : Part->GetObjectField(TEXT("ratings"))->Values)
            {
                const TOptional<EShipRating> Rating = ShipParts::RatingFromName(Rated.Key);
                if (!Rating)
                {
                    Out.Problems.Add(FString::Printf(TEXT("%s: no rating is called %s"), *Row.Spec.Id.ToString(), *Rated.Key));
                    continue;
                }
                Row.Spec.Ratings.Add(*Rating, Rated.Value->AsNumber());
            }
            Row.Asset = Part->GetStringField(TEXT("asset"));
            Row.Name = Part->GetStringField(TEXT("name"));
            Row.Words = Part->GetStringField(TEXT("words"));
            Out.Rows.Add(Row);
        }
        return Out;
    }

    inline TArray<FShipPartSpec> Specs(const FCatalogue& Catalogue)
    {
        TArray<FShipPartSpec> Out;
        for (const FRow& Row : Catalogue.Rows)
        {
            Out.Add(Row.Spec);
        }
        return Out;
    }

    /** Where the authoring script puts an asset, as LoadObject wants it. */
    inline FString ObjectPath(const FCatalogue& Catalogue, const FString& Asset)
    {
        return FString::Printf(TEXT("%s/%s.%s"), *Catalogue.Directory, *Asset, *Asset);
    }
}
```

- [ ] **Step 3: Write the failing test**

Add `#include "Tests/ShipPartsJson.h"` to `ShipPartsTest.cpp`'s includes. Then append, before the closing `#endif`:

```cpp
/*
 * Decision 7 over the whole catalogue: no loadout has a right answer. Every
 * combination is whole at rest under the stock reactor; every upgrade is at
 * least as open as stock on every axis of its bay; no part in a bay
 * dominates another (equal numbers do not); a core part rates only its own
 * bay; an aux part rates nothing and wants nothing at rest (decision 8).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCatalogueRulesTest, "DeepSpace.Ship.Parts.CatalogueRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipPartsTestLocal
{
    FShipPartSpec Part(const TCHAR* Id, EShipBay Bay, double Draw, const TArray<TPair<EShipRating, double>>& Ratings = {})
    {
        FShipPartSpec Spec;
        Spec.Id = Id;
        Spec.Bay = Bay;
        Spec.Draw = Draw;
        for (const TPair<EShipRating, double>& Rated : Ratings)
        {
            Spec.Ratings.Add(Rated.Key, Rated.Value);
        }
        return Spec;
    }

    bool Says(const TArray<FString>& Problems, const TCHAR* Needle)
    {
        return Problems.ContainsByPredicate([Needle](const FString& Problem) { return Problem.Contains(Needle); });
    }
}

bool FShipPartsCatalogueRulesTest::RunTest(const FString& Parameters)
{
    using namespace ShipPartsTestLocal;
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    TestTrue(FString::Printf(TEXT("the catalogue reads cleanly (%s)"), *FString::Join(Catalogue.Problems, TEXT("; "))),
             Catalogue.Problems.IsEmpty());
    const TArray<FShipPartSpec> Specs = ShipPartsJson::Specs(Catalogue);
    TestEqual(TEXT("six stock parts and the two examples"), Specs.Num(), 8);
    const TArray<FString> Problems = ShipParts::Validate(Specs);
    TestTrue(FString::Printf(TEXT("every rule holds over every row (%s)"), *FString::Join(Problems, TEXT("; "))), Problems.IsEmpty());

    // The stock loadout, the heaviest there is, asks 1370 W at rest.
    double Heaviest = 0.0;
    for (const EShipBay Bay : ShipBay::All())
    {
        double Worst = 0.0;
        for (const FShipPartSpec& Spec : Specs)
        {
            Worst = Spec.Bay == Bay ? FMath::Max(Worst, ShipParts::AtRestWatts(Spec)) : Worst;
        }
        Heaviest += Worst;
    }
    TestEqual(TEXT("the heaviest loadout asks 1370 W at rest"), Heaviest, 1370.0);

    // Each rule, broken once, is refused by name.
    const auto Refused = [&](const TCHAR* What, const FShipPartSpec& Added, const TCHAR* Needle)
    {
        TArray<FShipPartSpec> With = Specs;
        With.Add(Added);
        const TArray<FString> Found = ShipParts::Validate(With);
        TestTrue(FString::Printf(TEXT("%s is refused (%s)"), What, *FString::Join(Found, TEXT("; "))), Says(Found, Needle));
    };
    Refused(TEXT("a brighter lights part, wanting 350 W"),
            Part(TEXT("Lights.Bright"), EShipBay::Lights, 120.0, { { EShipRating::LightsWant, 350.0 } }), TEXT("at rest"));
    Refused(TEXT("a shorter array"),
            Part(TEXT("Sensors.Short"), EShipBay::Sensors, 200.0, { { EShipRating::RangeLy, 10.0 } }), TEXT("less open than stock"));
    Refused(TEXT("a slower drive than the quick lever on one axis and no better on any"),
            Part(TEXT("Drive.Middling"), EShipBay::Drive, 0.0, { { EShipRating::DriveResponse, 4.0 } }), TEXT("dominates"));
    Refused(TEXT("a reactor that rates the range"),
            Part(TEXT("Reactor.Odd"), EShipBay::Reactor, 0.0, { { EShipRating::RangeLy, 20.0 } }), TEXT("owns"));
    Refused(TEXT("an aux part with a number"),
            Part(TEXT("Aux.Booster"), EShipBay::Aux1, 0.0, { { EShipRating::RangeLy, 20.0 } }), TEXT("verbs"));
    Refused(TEXT("an aux part that draws at rest"),
            Part(TEXT("Aux.Lamp"), EShipBay::Aux1, 40.0), TEXT("wants nothing"));
    Refused(TEXT("a second part under a known id"),
            Part(TEXT("Reactor.TwinCore"), EShipBay::Reactor, 0.0, { { EShipRating::ReactorWatts, 1800.0 } }), TEXT("share the id"));
    Refused(TEXT("a part that fits no bay"), Part(TEXT("Nowhere.Part"), EShipBay::None, 0.0), TEXT("fits no bay"));
    {
        TArray<FShipPartSpec> Without = Specs;
        Without.RemoveAll([](const FShipPartSpec& Spec) { return Spec.Id == FName(TEXT("Sensors.Stock")); });
        TestTrue(TEXT("a core bay with no stock part is refused"), Says(ShipParts::Validate(Without), TEXT("no stock part")));
    }

    // Equal numbers do not dominate: a part that differs only in character is legal.
    {
        TArray<FShipPartSpec> Twins = Specs;
        Twins.Add(Part(TEXT("Drive.QuickLeverTwin"), EShipBay::Drive, 0.0,
                       { { EShipRating::WindingWant, 380.0 }, { EShipRating::ChargeSeconds, 45.0 }, { EShipRating::DriveResponse, 4.5 } }));
        const TArray<FString> Found = ShipParts::Validate(Twins);
        TestTrue(FString::Printf(TEXT("two parts with equal numbers are both legal (%s)"), *FString::Join(Found, TEXT("; "))), Found.IsEmpty());
    }
    // And two parts that trade one axis for another neither dominates.
    {
        TArray<FShipPartSpec> Traded = Specs;
        Traded.Add(Part(TEXT("Drive.Frugal"), EShipBay::Drive, 0.0, { { EShipRating::WindingWant, 300.0 } }));
        const TArray<FString> Found = ShipParts::Validate(Traded);
        TestTrue(FString::Printf(TEXT("a lower-want drive beside the quick lever is legal (%s)"), *FString::Join(Found, TEXT("; "))), Found.IsEmpty());
    }
    return true;
}
```

- [ ] **Step 4: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh
```

Expected: the build fails, `no member named 'Validate' in namespace 'ShipParts'`.

- [ ] **Step 5: The rules**

Append to `namespace ShipParts` in `Source/DeepSpace/Ship/ShipParts.h`, after `FindBay`:

```cpp
    /** One axis a bay's parts are compared on (decision 7's table): the
     *  draw, or one of its ratings. Every watt figure is more open when
     *  lower; every capability when higher. */
    struct FAxis
    {
        bool bDraw = false;
        EShipRating Rating = EShipRating::ReactorWatts;
        bool bHigherIsOpen = true;
    };

    /** Bay's axes: the draw in every core bay, and its ratings. None for an
     *  aux slot, whose parts rate nothing. */
    DEEPSPACE_API TArray<FAxis> AxesOf(EShipBay Bay);

    /** Part's value on Axis, signed so that more open is always larger; a
     *  rating the part lacks reads stock. */
    DEEPSPACE_API double Openness(const FShipPartSpec& Part, const FAxis& Axis);

    /** What Part asks at rest: its draw, and the want it rates (lights and
     *  boosters). The engine wants nothing at rest. */
    DEEPSPACE_API double AtRestWatts(const FShipPartSpec& Part);

    /**
     * Decision 7 over a catalogue, and decision 8's rules on a part: one
     * sentence per problem, empty when every rule holds. A design
     * constraint, not only a test: parts widen what the ship can do; they
     * never make it need more of anything.
     */
    DEEPSPACE_API TArray<FString> Validate(const TArray<FShipPartSpec>& Catalogue);
```

Append to `Source/DeepSpace/Ship/ShipParts.cpp`:

```cpp
TArray<ShipParts::FAxis> ShipParts::AxesOf(EShipBay Bay)
{
    const auto Rated = [](EShipRating Rating, bool bHigherIsOpen)
    {
        FAxis Axis;
        Axis.Rating = Rating;
        Axis.bHigherIsOpen = bHigherIsOpen;
        return Axis;
    };
    FAxis Draw;
    Draw.bDraw = true;
    Draw.bHigherIsOpen = false;
    switch (Bay)
    {
    case EShipBay::Reactor:     return { Rated(EShipRating::ReactorWatts, true), Draw };
    case EShipBay::Drive:       return { Rated(EShipRating::DriveResponse, true), Rated(EShipRating::ChargeSeconds, false),
                                         Rated(EShipRating::WindingWant, false), Draw };
    case EShipBay::Boosters:    return { Rated(EShipRating::LinearAcceleration, true), Rated(EShipRating::BoostersWant, false), Draw };
    case EShipBay::Lights:      return { Rated(EShipRating::LightsWant, false), Draw };
    case EShipBay::LifeSupport: return { Draw };
    case EShipBay::Sensors:     return { Rated(EShipRating::RangeLy, true), Draw };
    default:                    return {};
    }
}

double ShipParts::Openness(const FShipPartSpec& Part, const FAxis& Axis)
{
    const double* Rated = Axis.bDraw ? nullptr : Part.Ratings.Find(Axis.Rating);
    const double Value = Axis.bDraw ? Part.Draw : (Rated ? *Rated : FShipRatings::Stock().Get(Axis.Rating));
    return Axis.bHigherIsOpen ? Value : -Value;
}

double ShipParts::AtRestWatts(const FShipPartSpec& Part)
{
    FShipRatings Rated = FShipRatings::Stock();
    Apply(Rated, Part.Ratings);
    double Watts = Part.Draw;
    if (Part.Bay == EShipBay::Lights)
    {
        Watts += Rated.LightsWant;
    }
    if (Part.Bay == EShipBay::Boosters)
    {
        Watts += Rated.BoostersWant;
    }
    return Watts;
}

namespace ShipPartsDetail
{
    /** A at least as open as B on every axis of Bay and more open on one. */
    bool Dominates(const FShipPartSpec& A, const FShipPartSpec& B, EShipBay Bay)
    {
        bool bMore = false;
        for (const ShipParts::FAxis& Axis : ShipParts::AxesOf(Bay))
        {
            const double OpenA = ShipParts::Openness(A, Axis);
            const double OpenB = ShipParts::Openness(B, Axis);
            if (OpenA < OpenB)
            {
                return false;
            }
            bMore = bMore || OpenA > OpenB;
        }
        return bMore;
    }
}

TArray<FString> ShipParts::Validate(const TArray<FShipPartSpec>& Catalogue)
{
    TArray<FString> Problems;
    TSet<FName> Seen;
    for (const FShipPartSpec& Part : Catalogue)
    {
        const FString Id = Part.Id.ToString();
        if (Part.Id.IsNone())
        {
            Problems.Add(TEXT("a part has no id"));
        }
        else if (Seen.Contains(Part.Id))
        {
            Problems.Add(FString::Printf(TEXT("%s: two parts share the id"), *Id));
        }
        Seen.Add(Part.Id);
        if (Part.Bay == EShipBay::None)
        {
            Problems.Add(FString::Printf(TEXT("%s: fits no bay"), *Id));
            continue;
        }
        if (Part.Draw < 0.0)
        {
            Problems.Add(FString::Printf(TEXT("%s: a negative draw"), *Id));
        }
        for (const TPair<EShipRating, double>& Rated : Part.Ratings)
        {
            const FString Rating = RatingName(Rated.Key).ToString();
            if (ShipBay::IsAux(Part.Bay))
            {
                Problems.Add(FString::Printf(TEXT("%s: rates %s; aux parts add verbs, never numbers"), *Id, *Rating));
            }
            else if (OwnerOf(Rated.Key) != Part.Bay)
            {
                Problems.Add(FString::Printf(TEXT("%s: rates %s, which the %s bay owns"), *Id, *Rating,
                                             *ShipBay::Name(OwnerOf(Rated.Key)).ToString()));
            }
        }
        if (ShipBay::IsAux(Part.Bay) && Part.Draw > 0.0)
        {
            Problems.Add(FString::Printf(TEXT("%s: draws %.0f W at rest; an aux part wants nothing until its verb is used"), *Id, Part.Draw));
        }
    }

    for (const EShipBay Bay : ShipBay::All())
    {
        if (!ShipBay::IsCore(Bay))
        {
            continue;
        }
        const FShipPartSpec* Stock = Catalogue.FindByPredicate([Bay](const FShipPartSpec& Part)
        {
            return Part.Bay == Bay && Part.Id == ShipBay::StockPartId(Bay);
        });
        if (!Stock)
        {
            Problems.Add(FString::Printf(TEXT("the %s bay has no stock part (%s)"),
                                         *ShipBay::Name(Bay).ToString(), *ShipBay::StockPartId(Bay).ToString()));
            continue;
        }
        TArray<const FShipPartSpec*> Upgrades;
        for (const FShipPartSpec& Part : Catalogue)
        {
            if (Part.Bay == Bay && &Part != Stock)
            {
                Upgrades.Add(&Part);
            }
        }
        for (const FShipPartSpec* Part : Upgrades)
        {
            for (const FAxis& Axis : AxesOf(Bay))
            {
                if (Openness(*Part, Axis) < Openness(*Stock, Axis))
                {
                    Problems.Add(FString::Printf(TEXT("%s: less open than stock on %s"), *Part->Id.ToString(),
                                                 Axis.bDraw ? TEXT("its draw") : *RatingName(Axis.Rating).ToString()));
                }
            }
            for (const FShipPartSpec* Other : Upgrades)
            {
                if (Other != Part && ShipPartsDetail::Dominates(*Part, *Other, Bay))
                {
                    Problems.Add(FString::Printf(TEXT("%s dominates %s: a ladder, not a choice"),
                                                 *Part->Id.ToString(), *Other->Id.ToString()));
                }
            }
        }
    }

    // Every combination, one part per bay, whole at rest under the stock
    // reactor: the worst part of each bay together is the worst loadout.
    double Worst = 0.0;
    for (const EShipBay Bay : ShipBay::All())
    {
        double BayWorst = 0.0;
        for (const FShipPartSpec& Part : Catalogue)
        {
            const bool bFits = ShipBay::IsAux(Bay) ? ShipBay::IsAux(Part.Bay) : Part.Bay == Bay;
            BayWorst = bFits ? FMath::Max(BayWorst, AtRestWatts(Part)) : BayWorst;
        }
        Worst += BayWorst;
    }
    if (Worst > StockReactorWatts)
    {
        Problems.Add(FString::Printf(TEXT("the heaviest loadout asks %.0f W at rest, over the stock reactor's %.0f W"),
                                     Worst, StockReactorWatts));
    }
    return Problems;
}
```

- [ ] **Step 6: Run it and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh && ./test.sh DeepSpace.Ship.Parts.CatalogueRules
```

Expected: `Result: Succeeded`, then `passed: 1`.

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
git add Tools/ship_parts.json Source/DeepSpace/Tests/ShipPartsJson.h Source/DeepSpace/Ship/ShipParts.h \
        Source/DeepSpace/Ship/ShipParts.cpp Source/DeepSpace/Tests/ShipPartsTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(parts): the catalogue JSON, and decision 7's rules held over every row

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipParts.cpp 'if (Worst > StockReactorWatts)' 'if (Worst > 2.0 * StockReactorWatts)' DeepSpace.Ship.Parts.CatalogueRules && \
./build.sh
```

Expected: `KILLED` (the 350 W lights part is no longer refused "at rest"), then `Result: Succeeded`.

---

## Task 5 (D4): The authoring script, the assets, the game mode's list -- and the contract on three sides

**Owner:** D. **Depends on:** Task 4. Spec decisions 4, 5; Risks (*BP_DeepSpaceGameMode keeps the old list*, *Python may have no factory*).

On today's `main` the Blueprint holds no list of its own (`strings -a Content/Blueprints/BP_DeepSpaceGameMode.uasset` finds no `DA_` and no `Modules`): it inherits the C++ list from `DeepSpaceGameMode.cpp:11-15`. The spec's risk therefore reads, here: the script must *give* the Blueprint a saved list, and Step 5 checks that it did -- the six `_Stock` names are present -- before it counts the old names' absence, which on today's asset is `0` whether or not the script ran.

**Files:**
- Create: `Tools/setup_ship_parts.py`
- Create (by the script): `Content/Ship/Parts/DA_Reactor_Stock.uasset`, `DA_Reactor_TwinCore.uasset`, `DA_Drive_Stock.uasset`, `DA_Drive_QuickLever.uasset`, `DA_Boosters_Stock.uasset`, `DA_Lights_Stock.uasset`, `DA_LifeSupport_Stock.uasset`, `DA_Sensors_Stock.uasset`, `DA_ShipCatalogue.uasset`
- Delete (by the script): `Content/Ship/Modules/DA_LifeSupport.uasset`, `DA_Lights.uasset`, `DA_Sensors.uasset`
- Modify (by the script): `Content/Blueprints/BP_DeepSpaceGameMode.uasset` (`StartingModules`)
- Modify: `Source/DeepSpace/Tests/ShipPartsTest.cpp` (append `DeepSpace.Ship.Parts.Contract`)

**Interfaces:**
- Consumes: `Tools/ship_parts.json` (D3); `unreal.ShipModuleDataAsset`, `unreal.ShipPartCatalogue`, `unreal.ShipBay`, `unreal.ShipRating` (D1, D2, as Python sees them); `ShipPartsJson::*` (D3).
- Produces: the nine assets under `/Game/Ship/Parts`; `BP_DeepSpaceGameMode.StartingModules` = the six stock parts in bay order.

- [ ] **Step 1: Write the failing test**

Append to `Source/DeepSpace/Tests/ShipPartsTest.cpp`, before the closing `#endif`:

```cpp
/*
 * The catalogue on three sides, as DeepSpace.Sky.MaterialContract holds the
 * sky (decision 5): Tools/ship_parts.json, the assets the script authored
 * from it, and FShipRatings::Stock() against the stock rows. A settled
 * number goes into the JSON and the script is re-run; this fails until both
 * are done.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsContractTest, "DeepSpace.Ship.Parts.Contract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsContractTest::RunTest(const FString& Parameters)
{
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    if (!TestTrue(TEXT("the catalogue reads"), Catalogue.Problems.IsEmpty() && Catalogue.Rows.Num() > 0))
    {
        return false;
    }

    // -- the assets are the JSON ------------------------------------------------
    for (const ShipPartsJson::FRow& Row : Catalogue.Rows)
    {
        const FString Id = Row.Spec.Id.ToString();
        const FString Path = ShipPartsJson::ObjectPath(Catalogue, Row.Asset);
        const UShipModuleDataAsset* Part = LoadObject<UShipModuleDataAsset>(nullptr, *Path);
        if (!TestNotNull(FString::Printf(TEXT("%s is authored at %s"), *Id, *Path), Part))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("%s: its id"), *Id), Part->ModuleId, Row.Spec.Id);
        TestTrue(FString::Printf(TEXT("%s: its bay"), *Id), Part->Bay == Row.Spec.Bay);
        TestEqual(FString::Printf(TEXT("%s: its draw"), *Id), static_cast<double>(Part->PowerDraw), Row.Spec.Draw);
        TestEqual(FString::Printf(TEXT("%s: its name"), *Id), Part->DisplayName.ToString(), Row.Name);
        TestEqual(FString::Printf(TEXT("%s: its words"), *Id), Part->Words.ToString(), Row.Words);
        TestEqual(FString::Printf(TEXT("%s: as many ratings"), *Id), Part->Ratings.Num(), Row.Spec.Ratings.Num());
        for (const TPair<EShipRating, double>& Rated : Row.Spec.Ratings)
        {
            const double* Held = Part->Ratings.Find(Rated.Key);
            TestTrue(FString::Printf(TEXT("%s: %s is %g"), *Id, *ShipParts::RatingName(Rated.Key).ToString(), Rated.Value),
                     Held && *Held == Rated.Value);
        }
    }

    // -- the catalogue asset lists exactly them, in the JSON's order -------------
    const FString CataloguePath = ShipPartsJson::ObjectPath(Catalogue, Catalogue.CatalogueAsset);
    if (const UShipPartCatalogue* Asset = LoadObject<UShipPartCatalogue>(nullptr, *CataloguePath);
        TestNotNull(FString::Printf(TEXT("the catalogue is authored at %s"), *CataloguePath), Asset))
    {
        TestEqual(TEXT("it lists every row and nothing else"), Asset->Parts.Num(), Catalogue.Rows.Num());
        for (int32 Index = 0; Index < FMath::Min(Asset->Parts.Num(), Catalogue.Rows.Num()); ++Index)
        {
            TestEqual(FString::Printf(TEXT("row %d is %s"), Index, *Catalogue.Rows[Index].Asset),
                      Asset->Parts[Index].ToSoftObjectPath().ToString(), ShipPartsJson::ObjectPath(Catalogue, Catalogue.Rows[Index].Asset));
        }
    }

    // -- the stock rows are FShipRatings::Stock(), every owned rating explicit ---
    const FShipRatings Stock = FShipRatings::Stock();
    for (const EShipBay Bay : ShipBay::All())
    {
        if (!ShipBay::IsCore(Bay))
        {
            continue;
        }
        const ShipPartsJson::FRow* Row = Catalogue.Rows.FindByPredicate([Bay](const ShipPartsJson::FRow& Candidate)
        {
            return Candidate.Spec.Id == ShipBay::StockPartId(Bay);
        });
        if (!TestNotNull(FString::Printf(TEXT("the %s bay has a stock row"), *ShipBay::Name(Bay).ToString()), Row))
        {
            continue;
        }
        for (const EShipRating Rating : ShipParts::AllRatings())
        {
            if (ShipParts::OwnerOf(Rating) != Bay)
            {
                continue;
            }
            const double* Rated = Row->Spec.Ratings.Find(Rating);
            TestTrue(FString::Printf(TEXT("%s rates %s as Stock() does (%g)"), *Row->Spec.Id.ToString(),
                                     *ShipParts::RatingName(Rating).ToString(), Stock.Get(Rating)),
                     Rated && *Rated == Stock.Get(Rating));
        }
    }

    // -- today's three draws, carried over exactly ------------------------------
    double Draws = 0.0;
    for (const TCHAR* Id : { TEXT("LifeSupport.Stock"), TEXT("Lights.Stock"), TEXT("Sensors.Stock") })
    {
        const ShipPartsJson::FRow* Row = Catalogue.Rows.FindByPredicate([Id](const ShipPartsJson::FRow& Candidate)
        {
            return Candidate.Spec.Id == FName(Id);
        });
        Draws += Row ? Row->Spec.Draw : 0.0;
    }
    TestEqual(TEXT("life support, lights and sensors draw 620 W together, as the hand-made modules did"), Draws, 620.0);
    return true;
}
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh && ./test.sh DeepSpace.Ship.Parts.Contract
```

Expected: `FAILED`, with `Reactor.Stock is authored at /Game/Ship/Parts/DA_Reactor_Stock.DA_Reactor_Stock` among the errors: no asset exists yet.

- [ ] **Step 3: Write the authoring script**

Create `Tools/setup_ship_parts.py`:

```python
"""Author the ship's parts (wear and upgrades decision 5).

The script writes three things:
- one UShipModuleDataAsset per row of Tools/ship_parts.json, in
  Content/Ship/Parts;
- the catalogue DA_ShipCatalogue, which lists every part in the JSON's order;
- BP_DeepSpaceGameMode's StartingModules: the six stock parts, in bay order.

The JSON is the one source, because a .uasset is binary and a number settled
into one is invisible to git. Tune a part in the JSON and re-run this.
DeepSpace.Ship.Parts.Contract holds the JSON, the assets and
FShipRatings::Stock() to the same numbers.

The three hand-made modules in Content/Ship/Modules (DA_LifeSupport, DA_Lights
and DA_Sensors) are today's ship. The script reads their draws before it
writes anything, and it stops unless they equal the JSON's and sum to 620 W.
It copies each module to its stock part's path, rewrites the copy from the
JSON, and deletes the original once the game mode no longer holds it.

BP_DeepSpaceGameMode today saves no StartingModules of its own: a Blueprint
saves only what differs from its parent, so it inherits the C++ constructor's
list. This script gives it its own list, the six stock parts, and from then
on that saved list is what play and Tests/StockShip.h read. A stale saved
list would leave play on the old modules while every C++ default looked
right, which is why DeepSpace.Ship.Parts.StockIsToday holds the two equal.

Python has no factory of its own for these assets, so each new one is made
through DataAssetFactory with its class set. If that returns nothing, the run
stops and names the asset.

Run with the editor closed, through the machine-wide lock:

    . Tools/ue_lock.sh && ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" -run=pythonscript -script="$PWD/Tools/setup_ship_parts.py" \\
        -unattended -nopause -nosplash -NoLiveCoding

unreal.log does not reach stdout under the commandlet; the report is in
Saved/setup_ship_parts.txt.
"""

import json
import os
import re
import traceback

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CATALOGUE = json.load(open(os.path.join(HERE, "ship_parts.json")))
DIRECTORY = CATALOGUE["directory"]
CATALOGUE_ASSET = CATALOGUE["catalogue"]
MODE_BP = "/Game/Blueprints/BP_DeepSpaceGameMode"
OLD_DIRECTORY = "/Game/Ship/Modules"

# The hand-made modules, by the stock part each one becomes.
HAND_MADE = {
    "LifeSupport.Stock": OLD_DIRECTORY + "/DA_LifeSupport",
    "Lights.Stock": OLD_DIRECTORY + "/DA_Lights",
    "Sensors.Stock": OLD_DIRECTORY + "/DA_Sensors",
}
HAND_MADE_WATTS = 620.0

EAL = unreal.EditorAssetLibrary
log = []


def note(line):
    log.append(line)
    unreal.log(line)


def upper_snake(name):
    """A C++ enumerator as Python spells it: LifeSupport -> LIFE_SUPPORT,
    RangeLy -> RANGE_LY, Aux1 -> AUX1."""
    return re.sub(r"(?<!^)(?=[A-Z])", "_", name).upper()


def enum_value(enum, name):
    value = getattr(enum, upper_snake(name), None)
    if value is None:
        raise RuntimeError(f"{enum.__name__} has no {name} (looked for {upper_snake(name)})")
    return value


def path_of(asset):
    return f"{DIRECTORY}/{asset}"


def row_by_id(part_id):
    for row in CATALOGUE["parts"]:
        if row["id"] == part_id:
            return row
    raise RuntimeError(f"ship_parts.json has no row {part_id}")


def check_hand_made_draws():
    """Today's three draws, read wherever they are now (the old module, or
    the stock part a previous run made of it) before anything is written."""
    total = 0.0
    for part_id, old in HAND_MADE.items():
        row = row_by_id(part_id)
        where = old if EAL.does_asset_exist(old) else path_of(row["asset"])
        if not EAL.does_asset_exist(where):
            raise RuntimeError(f"{part_id}: neither {old} nor {where} exists, so today's draw cannot be read")
        watts = EAL.load_asset(where).get_editor_property("power_draw")
        note(f"{part_id}: {where} draws {watts:.1f} W; the JSON says {row['draw']}")
        if abs(watts - float(row["draw"])) > 1e-3:
            raise RuntimeError(f"{part_id}: the asset draws {watts} W and the JSON {row['draw']} W; "
                               f"set the JSON's draw to the asset's")
        total += watts
    if abs(total - HAND_MADE_WATTS) > 1e-3:
        raise RuntimeError(f"the hand-made modules draw {total} W together, not {HAND_MADE_WATTS}: "
                           f"today's ship is not the one the spec describes")


def copy_hand_made():
    """Each hand-made module copied to its stock part's path, keeping its
    class and draw. The original is deleted once nothing holds it."""
    for part_id, old in HAND_MADE.items():
        new = path_of(row_by_id(part_id)["asset"])
        if EAL.does_asset_exist(old) and not EAL.does_asset_exist(new):
            if not EAL.duplicate_asset(old, new):
                raise RuntimeError(f"could not copy {old} to {new}")
            note(f"copied {old} to {new}")


def ensure_asset(asset, cls):
    path = path_of(asset)
    if EAL.does_asset_exist(path):
        found = EAL.load_asset(path)
        if not isinstance(found, cls):
            raise RuntimeError(f"{path} is a {type(found).__name__}, not a {cls.__name__}")
        return found
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    made = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset, DIRECTORY, cls, factory)
    if made is None:
        raise RuntimeError(f"could not create {path} as a {cls.__name__}")
    note(f"created {path}")
    return made


def write_part(row):
    part = ensure_asset(row["asset"], unreal.ShipModuleDataAsset)
    part.set_editor_property("module_id", row["id"])
    part.set_editor_property("display_name", row["name"])
    part.set_editor_property("words", row["words"])
    part.set_editor_property("power_draw", float(row["draw"]))
    part.set_editor_property("bay", enum_value(unreal.ShipBay, row["bay"]))
    ratings = unreal.Map(unreal.ShipRating, float)
    for name, value in row["ratings"].items():
        ratings[enum_value(unreal.ShipRating, name)] = float(value)
    part.set_editor_property("ratings", ratings)
    if not EAL.save_loaded_asset(part, False):
        raise RuntimeError(f"could not save {row['asset']}")
    note(f"{row['id']}: {row['bay']}, draw {row['draw']} W, {row['ratings']}")
    return part


def write_catalogue(parts):
    catalogue = ensure_asset(CATALOGUE_ASSET, unreal.ShipPartCatalogue)
    catalogue.set_editor_property("parts", parts)
    if len(catalogue.get_editor_property("parts")) != len(parts):
        raise RuntimeError(f"{CATALOGUE_ASSET}.Parts did not take")
    if not EAL.save_loaded_asset(catalogue, False):
        raise RuntimeError(f"could not save {CATALOGUE_ASSET}")
    note(f"{CATALOGUE_ASSET}: {len(parts)} parts")


def write_game_mode(stock):
    bp = unreal.load_asset(MODE_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("starting_modules", stock)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    if len(cdo.get_editor_property("starting_modules")) != len(stock):
        raise RuntimeError("BP_DeepSpaceGameMode.StartingModules did not survive the compile")
    if not EAL.save_loaded_asset(bp, False):
        raise RuntimeError("could not save BP_DeepSpaceGameMode")
    note("BP_DeepSpaceGameMode.StartingModules: " + ", ".join(p.get_name() for p in stock))


def delete_hand_made():
    for old in HAND_MADE.values():
        if EAL.does_asset_exist(old):
            if not EAL.delete_asset(old):
                raise RuntimeError(f"could not delete {old}: something still references it")
            note(f"deleted {old}")
    if EAL.does_directory_exist(OLD_DIRECTORY) and not EAL.list_assets(OLD_DIRECTORY):
        EAL.delete_directory(OLD_DIRECTORY)
        note(f"deleted {OLD_DIRECTORY}")


def main():
    check_hand_made_draws()
    copy_hand_made()
    parts = [write_part(row) for row in CATALOGUE["parts"]]
    write_catalogue(parts)
    stock = [part for part, row in zip(parts, CATALOGUE["parts"]) if row["id"].endswith(".Stock")]
    if len(stock) != 6:
        raise RuntimeError(f"expected six stock parts, found {len(stock)}")
    write_game_mode(stock)
    delete_hand_made()
    note("DONE")


try:
    main()
except Exception:
    note("FAILED\n" + traceback.format_exc())
    raise
finally:
    with open(unreal.Paths.project_saved_dir() + "setup_ship_parts.txt", "w") as f:
        f.write("\n".join(log) + "\n")
```

- [ ] **Step 4: Run the script**

The library must be built first, because the commandlet loads the classes Task 3 added.

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./build.sh && . Tools/ue_lock.sh && \
ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_ship_parts.py" -unattended -nopause -nosplash -NoLiveCoding; \
cat Saved/setup_ship_parts.txt
```

Expected report:
- three `... draws N W; the JSON says N` lines;
- three `copied` lines;
- five `created` lines (the new parts) and one more for `DA_ShipCatalogue`;
- eight part lines, then `DA_ShipCatalogue: 8 parts`;
- `BP_DeepSpaceGameMode.StartingModules: DA_Reactor_Stock, DA_Drive_Stock, DA_Boosters_Stock, DA_Lights_Stock, DA_LifeSupport_Stock, DA_Sensors_Stock`;
- three `deleted` lines and `deleted /Game/Ship/Modules`;
- `DONE`.

That is a first run. On a re-run (the script reads the draws from the stock parts once the old modules are gone): three draw lines, **no** `copied`, `created` or `deleted` lines, the eight part lines, `DA_ShipCatalogue: 8 parts`, the `BP_DeepSpaceGameMode.StartingModules` line, then `DONE`.

If the run stops, act on its message:
- **`the asset draws X W and the JSON Y W`:** set that row's `draw` in `Tools/ship_parts.json` to X, re-run Task 4's test, amend Task 4's commit with the JSON, and run this step again.
- **`could not create`:** Python's `DataAssetFactory` refused the class. This is the spec's risk. Stop and report it: the fallback (duplicate a moved stock part and rewrite it) changes the script's structure.
- **`something still references it`:** another asset holds an old module. Name it from the editor's reference viewer, and stop and report it.

- [ ] **Step 5: Check the Blueprint holds the new list and none of the old**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
strings -a Content/Blueprints/BP_DeepSpaceGameMode.uasset | grep -oE 'DA_(Reactor|Drive|Boosters|Lights|LifeSupport|Sensors)_Stock' | sort -u; \
strings -a Content/Blueprints/BP_DeepSpaceGameMode.uasset | grep -cE 'DA_(Lights|LifeSupport|Sensors)\b'
```

Expected: first the six stock asset names, one per line -- the Blueprint now saves a list of its own. Fewer than six stops here: the script's save did not take, and the Blueprint still inherits the C++ list, which names the old modules the script has just deleted. Then `0`: no old module name. The `0` means something only after the six: on today's asset, which saves no list, it is `0` already.

- [ ] **Step 6: Run the contract and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && ./test.sh DeepSpace.Ship.Parts.Contract && ./test.sh DeepSpace.Ship.JumpCanWindAtFullSpeed
```

Expected: `passed: 1` each time. `JumpCanWindAtFullSpeed` installs the Blueprint's list through `InstallModule`, which still exists. Its draws are keyed by the new ids, and they still sum to 620 W.

**From this commit until Task 8 (S3) merges it, the C++ fallback list is broken on `feat/wear-1-d`:** `DeepSpaceGameMode.cpp:11-15` still names `/Game/Ship/Modules/DA_*`, which the script deleted, and `DeepSpaceGameMode.cpp` is S's file. Only the Blueprint's saved list (Step 5) is live. Nothing on D may call `GetDefault<ADeepSpaceGameMode>()->GetStartingModules()` directly; the three tests that read the list (`StockShip.h`, `JumpWindsTest.cpp:35-36`, `SliceChooseTest.cpp:446`) load the Blueprint first and fall back to C++ only if the Blueprint class fails to load, which Step 5 has ruled out.

- [ ] **Step 7: Commit, assets through LFS**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
git add Tools/setup_ship_parts.py Source/DeepSpace/Tests/ShipPartsTest.cpp Content/Ship/Parts \
        Content/Blueprints/BP_DeepSpaceGameMode.uasset && git add -A Content/Ship/Modules && \
git commit -m "$(cat <<'EOF'
feat(parts): setup_ship_parts.py authors the parts, the catalogue and the game mode's list

The three hand-made modules become Lights.Stock, LifeSupport.Stock and
Sensors.Stock with their draws unchanged (620 W); the reactor, the drive and
the boosters become parts; the twin core and the quick lever are the two
example upgrades. DeepSpace.Ship.Parts.Contract holds the JSON, the assets
and FShipRatings::Stock() to one set of numbers.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)" && git lfs ls-files | grep -c 'Content/Ship/Parts/'
```

Expected: the commit, then `9`: every new asset went through LFS, not into git as a blob.

- [ ] **Step 8: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-d && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipParts.cpp 'Ratings.ReactorWatts = ShipParts::StockReactorWatts;' 'Ratings.ReactorWatts = 1300.0;' DeepSpace.Ship.Parts.Contract && \
./build.sh
```

Expected: `KILLED` (Reactor.Stock rates 1400 in the JSON and the asset, and Stock() now says 1300), then `Result: Succeeded`.

---

## Task 6 (S1): The rated values come from the ratings; four CVars become overrides; the static getters move to the ship

**Owner:** S. **Depends on:** Task 2 (D1) committed. Spec decisions 2, 6; sign-offs 9, 29 (the one writer of the boosters' want).

**Files:**
- Create the tree; create `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (`DeepSpace.Ship.Parts.CVarOverrides`)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h`:
  - includes (line 7): add `Ship/ShipParts.h`;
  - the winding-want and chart-range getters (lines 338-349);
  - delete the constants block (lines 499-527: `DefaultReactorOutput`, `LightsWant`, `BoostersWant` and their comments). The reasoning is already beside `ShipParts::Stock*`, from D1.
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp`:
  - the four CVars (lines 24-37, 69-72, 88-91);
  - `NavNear`'s log (line 152);
  - `Initialize` (lines 370-383), `SetLightsOn` (lines 452-459) and `ApplyAllocation` (lines 471-528);
  - `GetWindingWant`/`GetChartRangeLy` (lines 1244-1252).
- Modify: `Source/DeepSpace/UI/NavigationWidget.cpp` (line 355), `Source/DeepSpace/Ship/ShipHumComponent.cpp` (lines 90-95)
- Modify tests, reads only: `Tests/HumComponentTest.cpp` (line 203), `Tests/SliceChooseTest.cpp` (lines 87-91), `Tests/JumpWindsTest.cpp` (lines 91-95, 117-118), `Tests/NavScreenTest.cpp` (lines 107-111), `Tests/ChartLayoutTest.cpp` (lines 416-422), `Tests/ShipJumpTest.cpp` (line 234)

**Interfaces:**
- Consumes: `FShipRatings`, `ShipParts::Effective`, `ShipParts::Stock*` (D1).
- Produces: `FShipRatings UShipSubsystem::GetRatings() const` (stock until S2), `float GetWindingWant() const`, `float GetChargeSeconds() const`, `float GetDriveResponse() const`, `float GetChartRangeLy() const`. The four CVars default to `-1`. `ApplyAllocation` becomes the boosters' want's one writer.

- [ ] **Step 1: The S tree**

```bash
cd /home/matt/Development/deepspace && \
D1=$(git log --format=%H -1 --fixed-strings \
     --grep='feat(parts): the pure core -- bays, ratings, the stock ship as numbers, the plain loadout state' feat/wear-1-d) && \
test -n "$D1" && git worktree add .worktrees/wear-1-s -b feat/wear-1-s "$D1" && git -C .worktrees/wear-1-s log --oneline -1
```

Expected: `Preparing worktree (new branch 'feat/wear-1-s')`, then D1's commit. The tree has D1 and nothing D committed after it: cut from the branch tip, it could already hold D4's asset rewrite, which S is not ready for until Task 8. If `test -n` fails, D1 is not committed yet: wait.

- [ ] **Step 2: Write the failing test**

Create `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`:

```cpp
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipNavState.h"
#include "Ship/ShipParts.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"
#include "Tests/SkyTestWorld.h"
#include "Universe/StarSystem.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Decision 6: the four numbers a part rates are read through the ship,
 * where a console variable overrides the fitted part for the session. -1,
 * the default, is the part; 0 or more is the override, 0 included; any other
 * negative is the part. What the flight and the jump run on is the same
 * number (review focus 1).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCVarOverridesTest, "DeepSpace.Ship.Parts.CVarOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPartsCVarOverridesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    FSkyWorld Test(TEXT("CVarOverridesWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();
    const FShipRatings Rated = Ship->GetRatings();

    struct FCase
    {
        const TCHAR* CVar;
        TFunction<float()> Read;
        double Part;
    };
    const FCase Cases[] = {
        { TEXT("ds.Nav.RangeLy"), [Ship]() { return Ship->GetChartRangeLy(); }, Rated.RangeLy },
        { TEXT("ds.Nav.ChargeSeconds"), [Ship]() { return Ship->GetChargeSeconds(); }, Rated.ChargeSeconds },
        { TEXT("ds.Nav.WindingWant"), [Ship]() { return Ship->GetWindingWant(); }, Rated.WindingWant },
        { TEXT("ds.Drive.Response"), [Ship]() { return Ship->GetDriveResponse(); }, Rated.DriveResponse },
    };
    for (const FCase& Case : Cases)
    {
        TestEqual(FString::Printf(TEXT("%s defaults to -1, the part's"), Case.CVar), CVarFloat(Case.CVar), -1.0f);
        TestEqual(FString::Printf(TEXT("%s at -1 reads the fitted part"), Case.CVar), static_cast<double>(Case.Read()), Case.Part);
        {
            FScopedCVar Set(Case.CVar, 15.0f);
            TestEqual(FString::Printf(TEXT("%s 15 reads 15"), Case.CVar), Case.Read(), 15.0f);
        }
        {
            FScopedCVar Zero(Case.CVar, 0.0f);
            TestEqual(FString::Printf(TEXT("%s 0 is an override too, and reads 0"), Case.CVar), Case.Read(), 0.0f);
        }
        {
            FScopedCVar Negative(Case.CVar, -5.0f);
            TestEqual(FString::Printf(TEXT("%s -5 reads the part, never itself"), Case.CVar), static_cast<double>(Case.Read()), Case.Part);
        }
        TestEqual(FString::Printf(TEXT("%s put back reads the part again"), Case.CVar), static_cast<double>(Case.Read()), Case.Part);
    }

    // The flight runs on the same number the getter answers.
    {
        FScopedCVar Response(TEXT("ds.Drive.Response"), 1.5f);
        Ship->Tick(0.01f);
        TestEqual(TEXT("the drive's ease runs on the override"), Ship->GetFlightState().GetLimits().DriveResponse, 1.5);
    }
    Ship->Tick(0.01f);
    TestEqual(TEXT("and on the part's response once it is put back"), Ship->GetFlightState().GetLimits().DriveResponse, Rated.DriveResponse);

    // And so does the jump.
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (TestTrue(TEXT("the chart has a system to plot"), Chart.Num() > 0 && Ship->PlotCourse(Chart[0].Id)))
    {
        if (const TOptional<FVector> Course = Ship->GetCourseDirection())
        {
            // Facing away, so a charged jump waits rather than firing.
            Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
        }
        FScopedCVar Want(TEXT("ds.Nav.WindingWant"), 200.0f);
        TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
        Ship->Tick(0.01f);
        TestEqual(TEXT("winding, the engine asks the override"), Ship->GetConsumerWant(ShipPower::Engine), 200.0f);
        Ship->SetJumpEngaged(false);
        Ship->Tick(0.01f);
    }
    return true;
}

#endif
```

- [ ] **Step 3: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh
```

Expected: the build fails, `no member named 'GetRatings' in 'UShipSubsystem'`.

- [ ] **Step 4: The getters, in the header**

In `Source/DeepSpace/Ship/ShipSubsystem.h`, add `#include "Ship/ShipParts.h"` after `#include "Ship/ShipNavState.h"`. Then replace the two declarations from `    /** Watts the engine asks for while the jump winds, ds.Nav.WindingWant as` through `    static float GetChartRangeLy();` with:

```cpp
    /** The ship's rated values: every fitted part's ratings over the stock
     *  part's (wear and upgrades decision 2), derived on every call and
     *  stored nowhere. An empty bay reads stock (decision 3). */
    FShipRatings GetRatings() const;

    /** Watts the engine asks for while the jump winds: the drive part's
     *  WindingWant, or ds.Nav.WindingWant when that is 0 or more (decision
     *  6); never negative. What the hum measures the engine's share against
     *  (plan conflict 8), asked here so the one number has one reader. */
    float GetWindingWant() const;

    /** Seconds for a full charge from cold at full feed: the drive part's
     *  ChargeSeconds, or ds.Nav.ChargeSeconds when set. Never on a screen:
     *  no screen shows the jump's charge. */
    float GetChargeSeconds() const;

    /** Notches a second the drive's ease may move at full thrust: the drive
     *  part's DriveResponse, or ds.Drive.Response when set. */
    float GetDriveResponse() const;

    /** How far the chart reaches, light years: the sensors part's RangeLy, or
     *  ds.Nav.RangeLy when set. GetChart's radius, for anything that must know
     *  when the chart's answer can have changed. */
    float GetChartRangeLy() const;
```

Delete, near the end of the class, the three constants and their comments: from `    /**` / `     * Placeholder reactor rating. Becomes a module later.` through `    static constexpr float BoostersWant = 450.0f;`. `StarvedBoosterThrust` and its comment stay.

- [ ] **Step 5: The CVars become overrides**

In `Source/DeepSpace/Ship/ShipSubsystem.cpp`'s anonymous namespace, replace the `CVarChargeSeconds` and `CVarWindingWant` definitions, together with the 380 W comment between them (lines 24-37), with:

```cpp
    // The four numbers a fitted part rates (wear and upgrades decision 6):
    // -1, the default, means "the part's", and 0 or more overrides it for
    // the session, so a playtest still moves each with no rebuild. The
    // settled 45 s and 380 W live in the catalogue now (Tools/ship_parts.json,
    // FShipRatings::Stock), and the ship's four getters are the one place the
    // rule is applied (ShipParts::Effective). Unlike the rest of this block,
    // a value play settles for these four is written back into the part's
    // row in Tools/ship_parts.json, never here: -1 stays their default.
    TAutoConsoleVariable<float> CVarChargeSeconds(
        TEXT("ds.Nav.ChargeSeconds"), -1.0f,
        TEXT("Seconds for the jump to wind from cold with the engine fully fed. -1: the drive part's."),
        ECVF_Default);

    TAutoConsoleVariable<float> CVarWindingWant(
        TEXT("ds.Nav.WindingWant"), -1.0f,
        TEXT("Watts the engine asks for while the jump winds; it asks for nothing otherwise. -1: the drive part's."),
        ECVF_Default);
```

Replace the `CVarRangeLy` definition with:

```cpp
    TAutoConsoleVariable<float> CVarRangeLy(
        TEXT("ds.Nav.RangeLy"), -1.0f,
        TEXT("How far the chart reaches, light years. -1: the sensors part's."),
        ECVF_Default);
```

Replace the `CVarDriveResponse` definition with:

```cpp
    TAutoConsoleVariable<float> CVarDriveResponse(
        TEXT("ds.Drive.Response"), -1.0f,
        TEXT("Notches a second the drive's speed may move at full thrust. Thin boosters slow the whole ease, never this. -1: the drive part's."),
        ECVF_Default);
```

In `NavNear`, replace `Chart.Num(), UShipSubsystem::GetChartRangeLy());` with `Chart.Num(), Ship->GetChartRangeLy());`.

- [ ] **Step 6: The ship reads the ratings**

Replace `UShipSubsystem::Initialize`'s body after `Super::Initialize(Collection);` with:

```cpp
    // Today's ship until a part says otherwise: an empty bay reads the stock
    // part (wear and upgrades decision 3), so a bare test world is exactly
    // the bare test world it always was.
    const FShipRatings Stock = FShipRatings::Stock();
    PowerState.SetReactorOutput(static_cast<float>(Stock.ReactorWatts));

    // An even split to start with, which is a starting point and not a
    // recommendation: every split is viable and none is correct. The engine
    // starts idle, wanting nothing, and so takes part in no split until the
    // jump is engaged.
    PowerState.SetConsumer(ShipPower::Lights, static_cast<float>(Stock.LightsWant), 1.0f);
    PowerState.SetConsumer(ShipPower::Boosters, static_cast<float>(Stock.BoostersWant), 1.0f);
    PowerState.SetConsumer(ShipPower::Engine, 0.0f, 1.0f);
```

In `SetLightsOn`, replace `PowerState.SetWant(ShipPower::Lights, bOn ? LightsWant : 0.0f);` with:

```cpp
    PowerState.SetWant(ShipPower::Lights, bOn ? static_cast<float>(GetRatings().LightsWant) : 0.0f);
```

In `ApplyAllocation`, make four changes:
1. Insert at the top of the body:

```cpp
    // The fitted parts' numbers, derived now and never stored (wear decision 2).
    const FShipRatings Ratings = GetRatings();

```

2. Replace the three lines from `const float BoosterFeed = PowerState.GetSatisfaction(ShipPower::Boosters);` to `const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * BoosterFeed;`, together with the comment above them (`// Asked for fresh every frame ...`), with:

```cpp
    // The boosters' want has one writer, here (wear sign-off 29): the fitted
    // part's rating. A fit changes the ratings and this pass writes the want
    // from them, so a fit and anything else that adds to the want -- landing's
    // hold -- never write it from two places in one frame.
    const float BoostersWantNow = static_cast<float>(Ratings.BoostersWant);
    if (PowerState.GetWant(ShipPower::Boosters) != BoostersWantNow)
    {
        PowerState.SetWant(ShipPower::Boosters, BoostersWantNow);
    }

    // Asked for fresh every frame and never stored. A cached satisfaction is
    // how two things that read the same allocation start disagreeing.
    const float BoosterFeed = PowerState.GetSatisfaction(ShipPower::Boosters);
    const float EngineFeed = PowerState.GetSatisfaction(ShipPower::Engine);
    const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * BoosterFeed;
```

3. Replace these two lines:
   `const FShipFlightLimits Rated = FShipFlightLimits::Cruise();`
   `Limits.LinearAcceleration = Rated.LinearAcceleration * Thrust;`
   with:

```cpp
    Limits.LinearAcceleration = Ratings.LinearAcceleration * Thrust;
```

   and replace `Limits.DriveResponse = FMath::Max(0.0f, CVarDriveResponse.GetValueOnGameThread());` with `Limits.DriveResponse = GetDriveResponse();`.

4. Replace `const double ChargeSeconds = CVarChargeSeconds.GetValueOnGameThread();` with `const double ChargeSeconds = GetChargeSeconds();`.

Replace the definitions of `UShipSubsystem::GetWindingWant()` and `UShipSubsystem::GetChartRangeLy()` (lines 1244-1252) with:

```cpp
FShipRatings UShipSubsystem::GetRatings() const
{
    return FShipRatings::Stock();
}

float UShipSubsystem::GetWindingWant() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().WindingWant, CVarWindingWant.GetValueOnGameThread()));
}

float UShipSubsystem::GetChargeSeconds() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().ChargeSeconds, CVarChargeSeconds.GetValueOnGameThread()));
}

float UShipSubsystem::GetDriveResponse() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().DriveResponse, CVarDriveResponse.GetValueOnGameThread()));
}

float UShipSubsystem::GetChartRangeLy() const
{
    return static_cast<float>(ShipParts::Effective(GetRatings().RangeLy, CVarRangeLy.GetValueOnGameThread()));
}
```

(`GetRatings` answers stock here. Task 7 (S2) derives it from the bays.)

- [ ] **Step 7: The callers ask the ship**

- `Source/DeepSpace/UI/NavigationWidget.cpp:355`: `Asked.RangeLy = UShipSubsystem::GetChartRangeLy();` becomes `Asked.RangeLy = Ship.GetChartRangeLy();`.
- `Source/DeepSpace/Ship/ShipHumComponent.cpp`, in `AskShip`: replace `UShipSubsystem::GetWindingWant()` with `Ship.GetWindingWant()`. Replace the three lines from `// Rated, not current: the subsystem's GetLinearAcceleration is already` through `const float Rated = static_cast<float>(FShipFlightLimits::Cruise().LinearAcceleration);` with:

```cpp
    // Rated, not current: the subsystem's GetLinearAcceleration is already
    // the boosters part's rating scaled by their allocation, which is the
    // fraction the hiss thins by.
    const float Rated = static_cast<float>(Ship.GetRatings().LinearAcceleration);
```

- [ ] **Step 8: The tests that read the four CVars raw, or called the statics, read the ship**

Each keeps its numbers. Only where it reads them changes:
- `Tests/HumComponentTest.cpp:203`: `const float WindingWant = CVarFloat(TEXT("ds.Nav.WindingWant"));` becomes `const float WindingWant = Ship->GetWindingWant();`. The `FScopedCVar Want(TEXT("ds.Nav.WindingWant"), 2.0f * WindingWant);` below it now sets 760, an override.
- `Tests/SliceChooseTest.cpp:89`: `const float Want = CVarFloat(TEXT("ds.Nav.WindingWant"));` becomes `const float Want = Ship.GetWindingWant();`.
- `Tests/JumpWindsTest.cpp:93` and `:95`: both `UShipSubsystem::GetWindingWant()` become `Ship->GetWindingWant()`.
- `Tests/JumpWindsTest.cpp:117-118`: the two lines become `const double ChargeSeconds = Ship->GetChargeSeconds();`.
- `Tests/ShipJumpTest.cpp:234`: `UShipSubsystem::GetWindingWant()` becomes `Ship->GetWindingWant()`.
- `Tests/NavScreenTest.cpp`: `ChartRangeCm()` (lines 107-111) has no ship in hand at one of its two callers. It becomes:

```cpp
    double ChartRangeCm()
    {
        // The rule the ship applies (wear decision 6), over the stock array
        // this test's ship carries: -1 reads the part.
        const IConsoleVariable* Range = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Nav.RangeLy"));
        return ShipParts::Effective(FShipRatings::Stock().RangeLy, Range ? Range->GetFloat() : -1.0f) * UniverseUnits::CmPerLightYear;
    }
```

- `Tests/ChartLayoutTest.cpp:416-422`: replace the lines from `const IConsoleVariable* Range = ...` to the `for` line with:

```cpp
        const float RangeLy = Test.Ship->GetChartRangeLy();
        TArray<FString> Distances;
        for (int32 Tenths = 0; Tenths <= FMath::CeilToInt(10.0 * RangeLy); ++Tenths)
```

- [ ] **Step 9: Run it and see it pass, and every test that moved**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.CVarOverrides && \
./test.sh DeepSpace.Ship.HumComponent && ./test.sh DeepSpace.Ship.JumpCanWindAtFullSpeed && ./test.sh DeepSpace.Ship.Jump && \
./test.sh DeepSpace.Ship.NavScreen && ./test.sh DeepSpace.UI.NavigationScreen && ./test.sh DeepSpace.UI.ChartLayout && \
./test.sh DeepSpace.UI.ChartAsksOnChange && ./test.sh DeepSpace.Loop && ./test.sh DeepSpace.Ship.Drive && \
./test.sh DeepSpace.Ship.CounterFrameJump && ./test.sh DeepSpace.UI.NoseCaret && ./test.sh DeepSpace.UI.HUDSpeed && \
./test.sh DeepSpace.UI.HUDBearing && ./test.sh DeepSpace.UI.TargetOverlay && ./test.sh DeepSpace.UI.SystemMapScreen
```

Expected: every run `passed: N` with no `FAILED`, where N ≥ 1. Their numbers are unchanged: stock is 12 ly, 45 s, 380 W and 3 notches/s. The last six are the tests that set `ds.Nav.ChargeSeconds` through `FScopedCVar` to 0 (`CounterFrameJump`, `NoseCaret`, `HUDSpeed`, `HUDBearing`, `TargetOverlay`) or 0.5 s (`SystemMapScreen`, at `SystemMapScreenTest.cpp:553` and `:592`): they now depend on 0 and a small positive value overriding the part (*Review Focus* 1), so a `>`/`>=` slip in `ShipParts::Effective` shows here, not first in Task 14's suite. `HumComponent` (0.5 s) and `NavScreen` (2 s) are already above.

- [ ] **Step 10: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/UI/NavigationWidget.cpp \
        Source/DeepSpace/Ship/ShipHumComponent.cpp Source/DeepSpace/Tests/ShipLoadoutTest.cpp Source/DeepSpace/Tests/HumComponentTest.cpp \
        Source/DeepSpace/Tests/SliceChooseTest.cpp Source/DeepSpace/Tests/JumpWindsTest.cpp Source/DeepSpace/Tests/ShipJumpTest.cpp \
        Source/DeepSpace/Tests/NavScreenTest.cpp Source/DeepSpace/Tests/ChartLayoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(ship): rated values from the ratings; four CVars default to -1, the part's

The reactor, the wants, the boosters' acceleration, the drive's response,
the charge and the range come from FShipRatings (stock until parts are
fitted). ds.Nav.RangeLy, .ChargeSeconds, .WindingWant and ds.Drive.Response
override the part when 0 or more. GetWindingWant and GetChartRangeLy are
instance calls now, since they depend on the ship. ApplyAllocation is the
boosters' want's one writer (sign-off 29).

Tests that read the four CVars raw or called the statics -- HumComponent,
SliceChoose, JumpWinds, ShipJump, NavScreen, ChartLayout -- read the
ship's getters instead, because a raw read is now -1; their numbers do not
change.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 11: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'return static_cast<float>(ShipParts::Effective(GetRatings().RangeLy, CVarRangeLy.GetValueOnGameThread()));' 'return static_cast<float>(GetRatings().RangeLy);' DeepSpace.Ship.Parts.CVarOverrides && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'Limits.DriveResponse = GetDriveResponse();' 'Limits.DriveResponse = ShipParts::StockDriveResponse;' DeepSpace.Ship.Parts.CVarOverrides && \
./build.sh
```

Expected: `KILLED` twice ("ds.Nav.RangeLy 15 reads 15", then "the drive's ease runs on the override"), then `Result: Succeeded`.

---

## Task 7 (S2): Bays in the subsystem -- one part per bay, draws by bay, spares, test loads

**Owner:** S. **Depends on:** Task 6, and Task 3 (D2) committed on `feat/wear-1-d`. Spec decisions 1, 3, 4, 8 (rules 3 and 5; test loads are not parts), 10 (a displaced part is a spare), 11; sign-offs 6, 7, 28, 29. Review focus 2, 3, 4, 5.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public: a new parts block after `GetInstalledModules`/`RemoveModule`, lines 80-88; private: members after `InstalledModules`, line 417)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` (`Initialize`; `GetRatings`; new definitions after `RemoveModule`, about line 837)
- Modify: `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (append `DeepSpace.Ship.Parts.EmptyBayIsStock`, `.OnePerBay`, `.WantsFollowTheFit`)

**Interfaces:**
- Consumes: `UShipModuleDataAsset::Bay`, `::Ratings`, `::PowerDraw`, `::ModuleId` (D2); `ShipBay::*`, `ShipParts::EmptyLoadout`, `FindBay`, `Apply` (D1).
- Produces: `bool FitPart(UShipModuleDataAsset* Part)`, `bool RemovePart(EShipBay Bay)`, `UShipModuleDataAsset* GetFittedPart(EShipBay Bay) const`, `const FShipLoadoutState& GetLoadoutState() const`, `const TArray<FShipPartState>& GetSpares() const`, `void AddLoad(FName Name, float Watts)`, `bool RemoveLoad(FName Name)`. `GetRatings()` now derives from the bays. The private members are `Loadout`, `KnownParts`, `Register`, `SlotFor`, `FitState`, `SetBayPart` and `PushRatings`.
- `InstallModule` and `RemoveModule` still exist here. They book draws under a module's id, beside the bays' keys. Task 8 (S3) retires them.

- [ ] **Step 1: Bring D2 in**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
D2=$(git log --format=%H -1 --fixed-strings \
     --grep='feat(parts): a module is a part -- its bay, words and ratings; UShipPartCatalogue' feat/wear-1-d) && \
test -n "$D2" && git merge -q --no-edit "$D2" && git log --oneline -1 && ls Content/Ship/Modules
```

Expected: a clean merge of D2's commit **by hash**, never the branch tip, and `DA_LifeSupport.uasset DA_Lights.uasset DA_Sensors.uasset` still listed. S2 may run while D4 runs; the tip could then bring in D4's deleted `Content/Ship/Modules/*` and rewritten `BP_DeepSpaceGameMode` while S's C++ fallback list still names `/Game/Ship/Modules`, and the pre-existing suite would run six new parts through `InstallModule`, a state no task tests. The tip is merged in Task 8 (S3). If `test -n` fails, D2 is not committed yet: wait.

- [ ] **Step 2: Write the failing tests**

In `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`, add `#include "Ship/ShipModuleDataAsset.h"` to the includes. Then append, before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsEmptyBayIsStockTest, "DeepSpace.Ship.Parts.EmptyBayIsStock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsOnePerBayTest, "DeepSpace.Ship.Parts.OnePerBay",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsWantsFollowTheFitTest, "DeepSpace.Ship.Parts.WantsFollowTheFit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    /** A part made in code, as a test's fixture: never in the catalogue. */
    UShipModuleDataAsset* MakePart(const TCHAR* Id, EShipBay Bay, float Draw, const TArray<TPair<EShipRating, double>>& Ratings,
                                   const TCHAR* Name = TEXT("A part"), const TCHAR* Words = TEXT("Quiet."))
    {
        UShipModuleDataAsset* Part = NewObject<UShipModuleDataAsset>();
        Part->ModuleId = Id;
        Part->Bay = Bay;
        Part->PowerDraw = Draw;
        Part->DisplayName = FText::FromString(Name);
        Part->Words = FText::FromString(Words);
        for (const TPair<EShipRating, double>& Rated : Ratings)
        {
            Part->Ratings.Add(Rated.Key, Rated.Value);
        }
        return Part;
    }

    /** Everything taken off the top before any split: the total draw less
     *  every consumer's share. */
    float Draws(const UShipSubsystem& Ship)
    {
        return Ship.GetPowerDraw() - (Ship.GetConsumerShare(ShipPower::Lights) + Ship.GetConsumerShare(ShipPower::Boosters)
                                      + Ship.GetConsumerShare(ShipPower::Engine));
    }

    FString SpareIds(const UShipSubsystem& Ship)
    {
        TArray<FString> Ids;
        for (const FShipPartState& Spare : Ship.GetSpares())
        {
            Ids.Add(Spare.PartId.ToString());
        }
        return FString::Join(Ids, TEXT(","));
    }
}

/*
 * Decision 3: a test world has no game mode and so fits nothing, and an
 * empty core bay reads the stock part and draws nothing -- so a bare world's
 * ship is exactly the bare world's ship it always was.
 */
bool FShipPartsEmptyBayIsStockTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("EmptyBayIsStockWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Ship->Tick(0.01f);
    for (const EShipBay Bay : ShipBay::All())
    {
        TestNull(FString::Printf(TEXT("a bare world fits nothing in %s"), *ShipBay::Name(Bay).ToString()), Ship->GetFittedPart(Bay));
    }
    const FShipRatings Stock = FShipRatings::Stock();
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        TestEqual(FString::Printf(TEXT("an empty bay reads the stock %s"), *ShipParts::RatingName(Rating).ToString()),
                  Ship->GetRatings().Get(Rating), Stock.Get(Rating));
    }
    TestEqual(TEXT("the reactor is today's 1400 W"), Ship->GetReactorOutput(), 1400.0f);
    TestEqual(TEXT("the lights want today's 300 W"), Ship->GetConsumerWant(ShipPower::Lights), 300.0f);
    TestEqual(TEXT("the boosters want today's 450 W"), Ship->GetConsumerWant(ShipPower::Boosters), 450.0f);
    TestEqual(TEXT("and nothing draws off the top"), Draws(*Ship), 0.0f, 1e-2f);
    TestEqual(TEXT("so the ship draws the two wants, 750 W"), Ship->GetPowerDraw(), 750.0f, 1e-2f);
    TestEqual(TEXT("the chart reaches 12 ly"), Ship->GetChartRangeLy(), 12.0f);
    TestEqual(TEXT("the jump winds on 380 W"), Ship->GetWindingWant(), 380.0f);
    TestEqual(TEXT("in 45 s"), Ship->GetChargeSeconds(), 45.0f);
    TestEqual(TEXT("and the drive follows at 3 notches a second"), Ship->GetDriveResponse(), 3.0f);
    TestFalse(TEXT("a core bay's part cannot be removed, only swapped"), Ship->RemovePart(EShipBay::Reactor));
    TestEqual(TEXT("and there are no spares"), Ship->GetSpares().Num(), 0);
    return true;
}

/*
 * Decision 1: one part per bay. A fit swaps, and the displaced part is a
 * spare; swapping back restores every rating; two parts never draw in one
 * bay; a part whose bay was never set is refused. And the classes the
 * spec's tests leave out: the part a bay already holds (review focus 2), the
 * auxiliary slots (focus 3), and parts with no id or another part's
 * (focus 4).
 */
bool FShipPartsOnePerBayTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("OnePerBayWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    UShipModuleDataAsset* A = MakePart(TEXT("Reactor.A"), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 1500.0 } });
    UShipModuleDataAsset* B = MakePart(TEXT("Reactor.B"), EShipBay::Reactor, 50.0f, { { EShipRating::ReactorWatts, 1600.0 } });

    TestTrue(TEXT("a reactor part fits the reactor bay"), Ship->FitPart(A));
    TestEqual(TEXT("and the ship runs on it at once"), Ship->GetReactorOutput(), 1500.0f);
    const FShipRatings WithA = Ship->GetRatings();
    TestEqual(TEXT("a fit into an empty bay displaces nothing"), Ship->GetSpares().Num(), 0);

    TestTrue(TEXT("a second reactor part fits the same bay"), Ship->FitPart(B));
    TestTrue(TEXT("by swapping: B is fitted"), Ship->GetFittedPart(EShipBay::Reactor) == B);
    TestEqual(TEXT("and A is a spare"), SpareIds(*Ship), FString(TEXT("Reactor.A")));
    TestEqual(TEXT("one part draws in one bay: B's 50 W, never A's too"), Draws(*Ship), 50.0f, 1e-2f);

    TestTrue(TEXT("swapping back"), Ship->FitPart(A));
    for (const EShipRating Rating : ShipParts::AllRatings())
    {
        TestEqual(FString::Printf(TEXT("restores %s exactly"), *ShipParts::RatingName(Rating).ToString()),
                  Ship->GetRatings().Get(Rating), WithA.Get(Rating));
    }
    TestEqual(TEXT("and A draws nothing, so nothing does"), Draws(*Ship), 0.0f, 1e-2f);

    // Review focus 2: the part the bay already holds.
    const FString SparesBefore = SpareIds(*Ship);
    TestTrue(TEXT("fitting the part the bay already holds is taken"), Ship->FitPart(A));
    TestEqual(TEXT("and changes nothing: no spare is made of it"), SpareIds(*Ship), SparesBefore);
    TestEqual(TEXT("and nothing is booked twice"), Draws(*Ship), 0.0f, 1e-2f);

    // A part that fits no bay; review focus 4: no id, or another part's id.
    TestFalse(TEXT("a part whose bay was never set is refused"),
              Ship->FitPart(MakePart(TEXT("Nowhere.Part"), EShipBay::None, 10.0f, {})));
    TestFalse(TEXT("a part with no id is refused: None in a bay means empty"),
              Ship->FitPart(MakePart(TEXT(""), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 1900.0 } })));
    TestFalse(TEXT("a different part under a known part's id is refused"),
              Ship->FitPart(MakePart(TEXT("Reactor.B"), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 2000.0 } })));
    TestFalse(TEXT("nothing is refused"), Ship->FitPart(nullptr));
    TestTrue(TEXT("and none of them displaced the reactor"),
             Ship->GetFittedPart(EShipBay::Reactor) == A && Ship->GetReactorOutput() == 1500.0f);
    TestEqual(TEXT("or touched the spares"), SpareIds(*Ship), SparesBefore);
    TestEqual(TEXT("or drew anything"), Draws(*Ship), 0.0f, 1e-2f);

    // Review focus 3: the auxiliary slots (decision 8's rules 3 and 5).
    UShipModuleDataAsset* X = MakePart(TEXT("Aux.X"), EShipBay::Aux1, 0.0f, {});
    UShipModuleDataAsset* Y = MakePart(TEXT("Aux.Y"), EShipBay::Aux1, 0.0f, {});
    UShipModuleDataAsset* Z = MakePart(TEXT("Aux.Z"), EShipBay::Aux1, 0.0f, {});
    TestTrue(TEXT("an aux part goes to the first free slot"), Ship->FitPart(X) && Ship->GetFittedPart(EShipBay::Aux1) == X);
    TestTrue(TEXT("a second to the other"), Ship->FitPart(Y) && Ship->GetFittedPart(EShipBay::Aux2) == Y);
    TestTrue(TEXT("with both full, a third replaces Aux1"), Ship->FitPart(Z) && Ship->GetFittedPart(EShipBay::Aux1) == Z);
    TestTrue(TEXT("and the part it displaced is a spare"), SpareIds(*Ship).EndsWith(TEXT("Aux.X")));
    const FString AuxSpares = SpareIds(*Ship);
    TestTrue(TEXT("one of a kind: a part already in the other slot is taken"), Ship->FitPart(Y));
    TestTrue(TEXT("and changes nothing"), Ship->GetFittedPart(EShipBay::Aux1) == Z && Ship->GetFittedPart(EShipBay::Aux2) == Y
                                          && SpareIds(*Ship) == AuxSpares);
    TestTrue(TEXT("an aux slot's part can be removed"), Ship->RemovePart(EShipBay::Aux2));
    TestNull(TEXT("leaving the slot empty"), Ship->GetFittedPart(EShipBay::Aux2));
    TestTrue(TEXT("and the part a spare"), SpareIds(*Ship).EndsWith(TEXT("Aux.Y")));
    TestFalse(TEXT("an empty slot has nothing to remove"), Ship->RemovePart(EShipBay::Aux2));
    TestFalse(TEXT("a core bay's part is never removed"), Ship->RemovePart(EShipBay::Reactor));
    TestTrue(TEXT("and the reactor was never touched by any of it"), Ship->GetFittedPart(EShipBay::Reactor) == A);
    return true;
}

/*
 * Review focus 5. A fit pushes the supply and the lights' want at once, but
 * never the boosters' want: ApplyAllocation is its one writer (sign-off 29),
 * so it arrives on the next pass. Switched off, a new lights part asks
 * nothing until the switch. And test loads are not parts (decision 8): a
 * plain draw, replaced by name, in no bay.
 */
bool FShipPartsWantsFollowTheFitTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("WantsFollowTheFitWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Ship->Tick(0.01f);

    TestTrue(TEXT("a lower-want boosters part fits"),
             Ship->FitPart(MakePart(TEXT("Boosters.Frugal"), EShipBay::Boosters, 0.0f, { { EShipRating::BoostersWant, 400.0 } })));
    TestEqual(TEXT("the fit does not write the boosters' want"), Ship->GetConsumerWant(ShipPower::Boosters), 450.0f);
    Ship->Tick(0.01f);
    TestEqual(TEXT("ApplyAllocation writes the part's on its next pass"), Ship->GetConsumerWant(ShipPower::Boosters), 400.0f);

    Ship->SetLightsOn(false);
    TestTrue(TEXT("a lower-want lights part fits with the lights off"),
             Ship->FitPart(MakePart(TEXT("Lights.Frugal"), EShipBay::Lights, 90.0f, { { EShipRating::LightsWant, 250.0 } })));
    TestEqual(TEXT("switched off, it asks nothing"), Ship->GetConsumerWant(ShipPower::Lights), 0.0f);
    Ship->Tick(0.01f);
    TestEqual(TEXT("and still nothing after a pass"), Ship->GetConsumerWant(ShipPower::Lights), 0.0f);
    TestEqual(TEXT("though its fittings draw off the top all the same"), Draws(*Ship), 90.0f, 1e-2f);
    Ship->SetLightsOn(true);
    TestEqual(TEXT("switched on, it asks its own want"), Ship->GetConsumerWant(ShipPower::Lights), 250.0f);

    TestTrue(TEXT("a reactor part fits"),
             Ship->FitPart(MakePart(TEXT("Reactor.Big"), EShipBay::Reactor, 0.0f, { { EShipRating::ReactorWatts, 1700.0 } })));
    TestEqual(TEXT("and the supply follows at once"), Ship->GetReactorOutput(), 1700.0f);

    Ship->AddLoad(TEXT("Test.Hog"), 500.0f);
    TestEqual(TEXT("a test load draws off the top"), Draws(*Ship), 590.0f, 1e-2f);
    Ship->AddLoad(TEXT("Test.Hog"), 300.0f);
    TestEqual(TEXT("the same load again replaces it, never adds"), Draws(*Ship), 390.0f, 1e-2f);
    TestTrue(TEXT("it is in no bay"), Ship->GetFittedPart(EShipBay::Aux1) == nullptr && Ship->GetFittedPart(EShipBay::Aux2) == nullptr);
    TestTrue(TEXT("it can be taken off"), Ship->RemoveLoad(TEXT("Test.Hog")));
    TestEqual(TEXT("leaving the lights' fittings"), Draws(*Ship), 90.0f, 1e-2f);
    TestFalse(TEXT("and it is gone"), Ship->RemoveLoad(TEXT("Test.Hog")));
    return true;
}
```

- [ ] **Step 3: Run them and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh
```

Expected: the build fails, `no member named 'GetFittedPart' in 'UShipSubsystem'`.

- [ ] **Step 4: The bays, in the header**

In `Source/DeepSpace/Ship/ShipSubsystem.h`, after the `RemoveModule` declaration, add:

```cpp
    // -- parts: one per bay (the wear and upgrades spec) -----------------------

    /**
     * Fits a new Part into the bay it declares -- an aux part into the first
     * free auxiliary slot, or Aux1 when both are full -- and the part it
     * displaces joins the spares, so every swap can be undone (decision 1).
     * The supply and the lights' want follow at once; the boosters' want on
     * the next ApplyAllocation, its one writer (sign-off 29).
     *
     * True with nothing changed when the bay already holds this part, and
     * when an aux part is already in the other slot (one of a kind). False,
     * and nothing changed, for null, a part whose Bay is None, a part with no
     * id (None in a bay means empty), and a different asset under an id this
     * ship already knows.
     */
    bool FitPart(UShipModuleDataAsset* Part);

    /** An auxiliary slot's part to the spares. Refused for a core bay: a core
     *  part is only ever swapped, so there is never a frame without a
     *  reactor (decision 3). */
    bool RemovePart(EShipBay Bay);

    /** The part in Bay; null for an empty bay, which reads the stock part's
     *  ratings and draws nothing. */
    UShipModuleDataAsset* GetFittedPart(EShipBay Bay) const;

    /** Every bay and every spare, plain and serialisable (decision 11): what
     *  the save will write. Ask rather than keep a copy. */
    const FShipLoadoutState& GetLoadoutState() const;

    /** The spares aboard, each one particular part. */
    const TArray<FShipPartState>& GetSpares() const;

    /**
     * A seam for tests, not a part (decision 8): a standing draw off the top
     * under "Load.<Name>", replaced if Name already draws. No console
     * command, no nameplate, no save -- as ds.Nav.FoldDraw books a draw
     * that is not a part. The hog tests starve the ship through it, because
     * a standing draw is exactly what an aux part may not have.
     */
    void AddLoad(FName Name, float Watts);

    /** False if Name was not drawing. */
    bool RemoveLoad(FName Name);
```

In the private section, after `TArray<TObjectPtr<UShipModuleDataAsset>> InstalledModules;`, add:

```cpp
    /** The fitted parts and the spares: the one truth about what is aboard,
     *  plain and serialisable (decision 11). Every bay is listed. */
    UPROPERTY()
    FShipLoadoutState Loadout;

    /** Every part asset this ship has fitted or been given, by id: what a
     *  PartId in Loadout resolves to. A part made in code resolves exactly
     *  as a catalogue part does, and nothing fitted is ever collected. */
    UPROPERTY()
    TMap<FName, TObjectPtr<UShipModuleDataAsset>> KnownParts;

    /** Refuses null, a None bay, no id, and a different asset under a known
     *  id; otherwise remembers Part under its id. */
    bool Register(UShipModuleDataAsset* Part);

    /** Where Part goes: its core bay, or the aux slot already holding it,
     *  else the first free aux slot, else Aux1. */
    TOptional<EShipBay> SlotFor(const UShipModuleDataAsset& Part) const;

    /** Part into Bay; whatever was there becomes a spare. */
    void FitState(EShipBay Bay, const FShipPartState& Part);

    /** Part into Bay, its draw re-booked under the bay's key, the ratings
     *  pushed. The one place a bay changes. */
    void SetBayPart(EShipBay Bay, const FShipPartState& Part);

    /** The supply and the lights' want, from the ratings. Never the
     *  boosters' want: ApplyAllocation writes that. */
    void PushRatings();
```

- [ ] **Step 5: The bays, in the implementation**

In `UShipSubsystem::Initialize`, after `Super::Initialize(Collection);`, add:

```cpp
    // Every bay, empty: each reads the stock part until something is fitted.
    Loadout = ShipParts::EmptyLoadout();
```

Replace the body of `UShipSubsystem::GetRatings()` with:

```cpp
    FShipRatings Ratings = FShipRatings::Stock();
    for (const FShipBayState& Entry : Loadout.Bays)
    {
        if (const TObjectPtr<UShipModuleDataAsset>* Part = KnownParts.Find(Entry.Part.PartId))
        {
            ShipParts::Apply(Ratings, (*Part)->Ratings);
        }
    }
    return Ratings;
```

After `UShipSubsystem::RemoveModule`'s definition, add:

```cpp
bool UShipSubsystem::Register(UShipModuleDataAsset* Part)
{
    if (!Part || Part->Bay == EShipBay::None || Part->ModuleId.IsNone())
    {
        UE_LOG(LogTemp, Warning, TEXT("Ship: refused %s: a part needs a bay and an id."),
               Part ? *Part->GetName() : TEXT("nothing"));
        return false;
    }
    const TObjectPtr<UShipModuleDataAsset>* Known = KnownParts.Find(Part->ModuleId);
    if (Known && Known->Get() != Part)
    {
        UE_LOG(LogTemp, Warning, TEXT("Ship: refused %s: %s already names %s."),
               *Part->GetName(), *Part->ModuleId.ToString(), *(*Known)->GetName());
        return false;
    }
    KnownParts.Add(Part->ModuleId, Part);
    return true;
}

TOptional<EShipBay> UShipSubsystem::SlotFor(const UShipModuleDataAsset& Part) const
{
    if (ShipBay::IsCore(Part.Bay))
    {
        return Part.Bay;
    }
    if (!ShipBay::IsAux(Part.Bay))
    {
        return {};
    }
    // One of a kind (decision 8, rule 3): the slot already holding this part
    // is the one it goes in, so the two slots never hold it twice.
    for (const EShipBay Aux : { EShipBay::Aux1, EShipBay::Aux2 })
    {
        if (ShipParts::FindBay(Loadout, Aux)->Part.PartId == Part.ModuleId)
        {
            return Aux;
        }
    }
    for (const EShipBay Aux : { EShipBay::Aux1, EShipBay::Aux2 })
    {
        if (ShipParts::FindBay(Loadout, Aux)->Part.PartId.IsNone())
        {
            return Aux;
        }
    }
    return EShipBay::Aux1;
}

bool UShipSubsystem::FitPart(UShipModuleDataAsset* Part)
{
    if (!Register(Part))
    {
        return false;
    }
    const TOptional<EShipBay> Slot = SlotFor(*Part);
    if (!Slot)
    {
        return false;
    }
    if (ShipParts::FindBay(Loadout, *Slot)->Part.PartId == Part->ModuleId)
    {
        // Already fitted: nothing to swap, and no spare to make of it.
        return true;
    }
    FShipPartState Fresh;
    Fresh.PartId = Part->ModuleId;
    FitState(*Slot, Fresh);
    return true;
}

void UShipSubsystem::FitState(EShipBay Bay, const FShipPartState& Part)
{
    const FShipPartState Displaced = ShipParts::FindBay(Loadout, Bay)->Part;
    if (!Displaced.PartId.IsNone())
    {
        Loadout.Spares.Add(Displaced);
    }
    SetBayPart(Bay, Part);
}

bool UShipSubsystem::RemovePart(EShipBay Bay)
{
    if (!ShipBay::IsAux(Bay))
    {
        return false;
    }
    const FShipPartState Removed = ShipParts::FindBay(Loadout, Bay)->Part;
    if (Removed.PartId.IsNone())
    {
        return false;
    }
    Loadout.Spares.Add(Removed);
    SetBayPart(Bay, FShipPartState());
    return true;
}

void UShipSubsystem::SetBayPart(EShipBay Bay, const FShipPartState& Part)
{
    ShipParts::FindBay(Loadout, Bay)->Part = Part;

    // Booked by bay, never by part (decision 4): a swap is one key's
    // RemoveDraw and AddDraw, so two parts in one bay can never both draw.
    const FName Key = ShipBay::DrawKey(Bay);
    PowerState.RemoveDraw(Key);
    const UShipModuleDataAsset* Fitted = GetFittedPart(Bay);
    if (Fitted && Fitted->PowerDraw > 0.0f)
    {
        PowerState.AddDraw(Key, Fitted->PowerDraw);
    }
    PushRatings();
}

void UShipSubsystem::PushRatings()
{
    const FShipRatings Rated = GetRatings();
    PowerState.SetReactorOutput(static_cast<float>(Rated.ReactorWatts));
    if (bLightsOn)
    {
        PowerState.SetWant(ShipPower::Lights, static_cast<float>(Rated.LightsWant));
    }
    // Not the boosters' want: ApplyAllocation is its one writer (sign-off
    // 29), and writes it from these ratings on its next pass.
}

UShipModuleDataAsset* UShipSubsystem::GetFittedPart(EShipBay Bay) const
{
    const FShipBayState* Entry = ShipParts::FindBay(Loadout, Bay);
    const TObjectPtr<UShipModuleDataAsset>* Part = Entry ? KnownParts.Find(Entry->Part.PartId) : nullptr;
    return Part ? Part->Get() : nullptr;
}

const FShipLoadoutState& UShipSubsystem::GetLoadoutState() const
{
    return Loadout;
}

const TArray<FShipPartState>& UShipSubsystem::GetSpares() const
{
    return Loadout.Spares;
}

void UShipSubsystem::AddLoad(FName Name, float Watts)
{
    const FName Key(*(FString(TEXT("Load.")) + Name.ToString()));
    PowerState.RemoveDraw(Key);
    PowerState.AddDraw(Key, FMath::Max(0.0f, Watts));
}

bool UShipSubsystem::RemoveLoad(FName Name)
{
    return PowerState.RemoveDraw(FName(*(FString(TEXT("Load.")) + Name.ToString())));
}
```

- [ ] **Step 6: Run them and see them pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.EmptyBayIsStock && \
./test.sh DeepSpace.Ship.Parts.OnePerBay && ./test.sh DeepSpace.Ship.Parts.WantsFollowTheFit && ./test.sh DeepSpace.Ship.Parts.CVarOverrides
```

Expected: `passed: 1` four times.

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipLoadoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(ship): one part per bay -- FitPart swaps, draws by bay, spares, AddLoad for tests

An empty bay reads the stock part and draws nothing, so a bare world is
unchanged. The boosters' want is written only by ApplyAllocation.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove each can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '    FShipRatings Ratings = FShipRatings::Stock();' '    FShipRatings Ratings;' DeepSpace.Ship.Parts.EmptyBayIsStock && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'Loadout.Spares.Add(Displaced);' '(void)Displaced;' DeepSpace.Ship.Parts.OnePerBay && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'if (ShipParts::FindBay(Loadout, *Slot)->Part.PartId == Part->ModuleId)' 'if (false)' DeepSpace.Ship.Parts.OnePerBay && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'PowerState.SetWant(ShipPower::Boosters, BoostersWantNow);' 'PowerState.SetWant(ShipPower::Boosters, 450.0f);' DeepSpace.Ship.Parts.WantsFollowTheFit && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '    if (bLightsOn)
    {
        PowerState.SetWant(ShipPower::Lights, static_cast<float>(Rated.LightsWant));' '    if (true)
    {
        PowerState.SetWant(ShipPower::Lights, static_cast<float>(Rated.LightsWant));' DeepSpace.Ship.Parts.WantsFollowTheFit && \
./build.sh
```

Expected: `KILLED` five times, then `Result: Succeeded`:
1. the stock ratings read zero;
2. A is not a spare;
3. refitting A makes a spare of it (review focus 2);
4. the boosters' want never reaches 400;
5. the lights ask 250 W while off (review focus 5).

---

## Task 8 (S3): The stock ship is six parts -- the game mode, StockShip, and InstallModule retired

**Owner:** S. **Depends on:** Task 7, and Task 5 (D4) committed on `feat/wear-1-d`. Spec decisions 2, 5, 8 (the hogs through `AddLoad`); sign-off 28.

**Files:**
- Modify: `Source/DeepSpace/Core/DeepSpaceGameMode.h` (the `StartingModules` comment, lines 22-29), `Source/DeepSpace/Core/DeepSpaceGameMode.cpp` (the paths, lines 7-16; `BeginPlay`'s loop, lines 38-54)
- Modify: `Source/DeepSpace/Tests/StockShip.h` (whole file)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (delete `InstallModule`, `RemoveModule`, the inline `GetInstalledModules` and `InstalledModules`, lines 79-88 and 416-417; declare the new `GetInstalledModules`)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` (delete `InstallModule`/`RemoveModule`, lines 810-836; define `GetInstalledModules`)
- Modify: `Source/DeepSpace/Tests/JumpWindsTest.cpp` (lines 83-87, 150-153), `Tests/SliceChooseTest.cpp` (lines 449-452), `Tests/LampPanelsDimTest.cpp` (lines 220-237), `Tests/ShipSkyTest.cpp` (lines 667-687)
- Modify: `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (append `DeepSpace.Ship.Parts.StockIsToday`, `.RatingsFollowParts`)

**Interfaces:**
- Consumes: the six stock assets and the two upgrades, and the Blueprint's list (D4). `FitPart`, `AddLoad` and `RemoveLoad` (S2).
- Produces: `TArray<UShipModuleDataAsset*> UShipSubsystem::GetInstalledModules() const` (a read-only view, in bay order). `StockShip::Install(UShipSubsystem*)` keeps its signature and fits the six parts; landing (b) and (c) call it unchanged. `InstallModule` and `RemoveModule` no longer exist.

- [ ] **Step 1: Bring D4 in, and find anything that looked a module up by its old id**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && git merge -q --no-edit feat/wear-1-d && ls Content/Ship/Parts && \
grep -rnE 'ModuleId ==|TEXT\("(Lights|LifeSupport|Sensors)"\)|Ship/Modules' Source Tools Config
```

Expected: a clean merge, nine `DA_*.uasset` files listed, and then the spec's *Risks* grep ("Renamed `ModuleId`s strand anything that looked a module up by its old id ... a grep in slice 1 confirms") with exactly these hits and no others:
- `Source/DeepSpace/Core/DeepSpaceGameMode.cpp:12-14`: the three old paths, which Step 4 replaces;
- `Source/DeepSpace/Ship/ShipParts.cpp`: `ShipBay::Name`'s `TEXT("Lights")`, `TEXT("LifeSupport")`, `TEXT("Sensors")` -- bay names, which is what a bay is saved by, not module ids;
- `Source/DeepSpace/Tests/ShipPowerStateTest.cpp:26-46`: six lines keying a hand-built `FShipPowerState`'s draws by plain names -- the pure arithmetic's own keys, never a module lookup;
- `Tools/setup_ship_parts.py`: its docstring's `Content/Ship/Modules` and `OLD_DIRECTORY`, the script's own record of what it replaced.

The only lookups by id the spec expects are the test hogs (`Test.*`), made in code, which this grep does not match. Any other hit looks a module up by an id this slice renamed: stop and report it, since that caller needs a step of its own.

- [ ] **Step 2: Write the failing tests**

In `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`, add `#include "Core/DeepSpaceGameMode.h"`, `#include "Ship/ShipFlightState.h"` and `#include "Tests/StockShip.h"` to the includes. Then append, before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsStockIsTodayTest, "DeepSpace.Ship.Parts.StockIsToday",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsRatingsFollowPartsTest, "DeepSpace.Ship.Parts.RatingsFollowParts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    UShipModuleDataAsset* LoadPart(const TCHAR* Asset)
    {
        return LoadObject<UShipModuleDataAsset>(nullptr, *FString::Printf(TEXT("/Game/Ship/Parts/%s.%s"), Asset, Asset));
    }

    /** Seconds from the drive's top to rest after an all stop, flown in a
     *  flight state of its own on Limits: no floor to hold it back, full
     *  thrust. -1 if it never reached the top. */
    double SecondsToRestFromTop(FShipFlightLimits Limits)
    {
        Limits.DriveThrust = 1.0;
        FShipFlightState Flight;
        Flight.SetLimits(Limits);
        FShipFlightCommand Command;
        Command.bDrive = true;
        Command.DriveNotch = Flight.GetDriveNotchCount() - 1;
        Flight.SetCommand(Command);
        constexpr double Step = 1.0 / 60.0;
        for (int32 Frame = 0; Frame < 60 * 120 && Flight.GetSpeed() < 0.999 * Limits.DriveTop; ++Frame)
        {
            Flight.Step(Step);
        }
        if (Flight.GetSpeed() < 0.999 * Limits.DriveTop)
        {
            return -1.0;
        }
        Command.DriveNotch = 0;
        Flight.SetCommand(Command);
        double Seconds = 0.0;
        while (Flight.GetSpeed() > 100.0 && Seconds < 120.0)
        {
            Flight.Step(Step);
            Seconds += Step;
        }
        return Seconds;
    }
}

/*
 * Ruling 1: the ship play flies is today's ship, bit for bit, now as six
 * parts. On StockShip::Install -- the Blueprint's list, which overrides the
 * C++ one -- the supply is 1400 W, the ship asks 1370 W at rest, the chart
 * reaches 12 ly and a full charge takes 45 s at full feed.
 */
bool FShipPartsStockIsTodayTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("StockIsTodayWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();

    // The C++ list and the Blueprint's are the same six, so neither can be stale.
    TArray<FString> Cpp;
    for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : GetDefault<ADeepSpaceGameMode>()->GetStartingModules())
    {
        Cpp.Add(Soft.ToSoftObjectPath().ToString());
    }
    TArray<FString> Play;
    for (const UShipModuleDataAsset* Part : StockShip::Modules())
    {
        Play.Add(FSoftObjectPath(Part).ToString());
    }
    TestEqual(TEXT("play starts with six parts"), Play.Num(), 6);
    TestEqual(TEXT("and the C++ default list is the Blueprint's"), FString::Join(Cpp, TEXT(",")), FString::Join(Play, TEXT(",")));

    TestEqual(TEXT("all six fit"), StockShip::Install(Ship), 6);
    for (const EShipBay Bay : ShipBay::All())
    {
        const UShipModuleDataAsset* Part = Ship->GetFittedPart(Bay);
        if (ShipBay::IsCore(Bay))
        {
            TestTrue(FString::Printf(TEXT("the %s bay holds %s"), *ShipBay::Name(Bay).ToString(), *ShipBay::StockPartId(Bay).ToString()),
                     Part && Part->ModuleId == ShipBay::StockPartId(Bay));
        }
        else
        {
            TestNull(FString::Printf(TEXT("%s is empty"), *ShipBay::Name(Bay).ToString()), Part);
        }
    }
    Ship->Tick(0.01f);
    TestEqual(TEXT("the supply is 1400 W"), Ship->GetReactorOutput(), 1400.0f);
    TestEqual(TEXT("620 W is drawn off the top"), Draws(*Ship), 620.0f, 1e-2f);
    TestEqual(TEXT("and the ship asks 1370 W at rest"), Ship->GetPowerDraw(), 1370.0f, 1e-2f);
    TestEqual(TEXT("whole at rest: the lights"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f);
    TestEqual(TEXT("and the boosters"), Ship->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f);
    TestEqual(TEXT("which push their full 2 km/s^2"), Ship->GetLinearAcceleration(), 2.0e5f);
    TestEqual(TEXT("the chart reaches 12 ly"), Ship->GetChartRangeLy(), 12.0f);
    TestEqual(TEXT("the jump winds on 380 W"), Ship->GetWindingWant(), 380.0f);
    TestEqual(TEXT("in 45 s at full feed"), Ship->GetChargeSeconds(), 45.0f);
    TestEqual(TEXT("and the drive follows at 3 notches a second"), Ship->GetDriveResponse(), 3.0f);
    TestEqual(TEXT("the view of the fitted parts lists the six"), Ship->GetInstalledModules().Num(), 6);
    return true;
}

/*
 * The two example upgrades change their bay's numbers and nothing else. The
 * twin core winds the jump fully fed with everything whole (sign-off 5); the
 * quick lever brings the ship to rest from 0.1 c sooner, measured through
 * the flight state (sign-off 24).
 */
bool FShipPartsRatingsFollowPartsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("RatingsFollowPartsWorld"));
    UShipSubsystem* Ship = Test.Ship;
    UShipModuleDataAsset* TwinCore = LoadPart(TEXT("DA_Reactor_TwinCore"));
    UShipModuleDataAsset* ReactorStock = LoadPart(TEXT("DA_Reactor_Stock"));
    UShipModuleDataAsset* QuickLever = LoadPart(TEXT("DA_Drive_QuickLever"));
    UShipModuleDataAsset* DriveStock = LoadPart(TEXT("DA_Drive_Stock"));
    if (!TestNotNull(TEXT("the world has a ship"), Ship) || !TestNotNull(TEXT("the twin core is authored"), TwinCore)
        || !TestNotNull(TEXT("the quick lever is authored"), QuickLever) || !TestNotNull(TEXT("and both stock parts"), ReactorStock)
        || !TestNotNull(TEXT("the stock drive"), DriveStock))
    {
        return false;
    }
    Test.BeginPlay();
    TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);

    // -- the twin core ------------------------------------------------------------
    TestTrue(TEXT("the twin core fits"), Ship->FitPart(TwinCore));
    TestEqual(TEXT("and the supply is 1800 W"), Ship->GetReactorOutput(), 1800.0f);
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!TestTrue(TEXT("a course can be plotted"), Chart.Num() > 0 && Ship->PlotCourse(Chart[0].Id)))
    {
        return false;
    }
    if (const TOptional<FVector> Course = Ship->GetCourseDirection())
    {
        Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
    }
    TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
    Ship->Tick(0.01f);
    Ship->Tick(0.01f);
    TestTrue(TEXT("and winds"), Ship->GetJumpState() == EJumpState::Winding);
    TestEqual(TEXT("fully fed on the twin core, at the default split"), Ship->GetConsumerSatisfaction(ShipPower::Engine), 1.0f, 1e-4f);
    TestEqual(TEXT("with the lights whole"), Ship->GetConsumerSatisfaction(ShipPower::Lights), 1.0f, 1e-4f);
    TestEqual(TEXT("and the boosters whole"), Ship->GetConsumerSatisfaction(ShipPower::Boosters), 1.0f, 1e-4f);
    TestTrue(TEXT("the stock reactor fits back"), Ship->FitPart(ReactorStock));
    Ship->Tick(0.01f);
    TestTrue(TEXT("and on it the split bites again: the lights dim while the jump winds"),
             Ship->GetConsumerSatisfaction(ShipPower::Lights) < 1.0f - 1e-3f);
    Ship->SetJumpEngaged(false);
    Ship->Tick(0.01f);

    // -- the quick lever ------------------------------------------------------------
    TestTrue(TEXT("the quick lever fits"), Ship->FitPart(QuickLever));
    Ship->Tick(0.01f);
    TestEqual(TEXT("and the drive follows at 4.5 notches a second"), Ship->GetDriveResponse(), 4.5f);
    TestEqual(TEXT("which the flight is handed"), Ship->GetFlightState().GetLimits().DriveResponse, 4.5);
    TestEqual(TEXT("and the reactor's number is its own, untouched"), Ship->GetReactorOutput(), 1400.0f);
    const double Quick = SecondsToRestFromTop(Ship->GetFlightState().GetLimits());
    TestTrue(TEXT("the stock drive fits back"), Ship->FitPart(DriveStock));
    Ship->Tick(0.01f);
    TestEqual(TEXT("and the response is 3 again"), Ship->GetFlightState().GetLimits().DriveResponse, 3.0);
    const double Stock = SecondsToRestFromTop(Ship->GetFlightState().GetLimits());
    AddInfo(FString::Printf(TEXT("from 0.1 c to rest after X: %.2f s on the quick lever, %.2f s on the stock drive"), Quick, Stock));
    TestTrue(TEXT("both reach the top"), Quick > 0.0 && Stock > 0.0);
    TestTrue(TEXT("and the quick lever brings the ship to rest sooner"), Quick < Stock);
    return true;
}
```

- [ ] **Step 3: Run them and see them fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.StockIsToday
```

Expected: `FAILED`. `StockShip::Install` still calls `InstallModule`, so `the Reactor bay holds Reactor.Stock` fails and the view lists none. The C++ list still names `/Game/Ship/Modules`, so `the C++ default list is the Blueprint's` fails too.

- [ ] **Step 4: The game mode fits parts**

In `Source/DeepSpace/Core/DeepSpaceGameMode.cpp`, replace the `DefaultModulePaths` array and its comment with:

```cpp
    // The stock ship, one part per core bay (wear and upgrades decision 2).
    // Paths rather than hard references so the game mode does not force these
    // assets to load with the class. BP_DeepSpaceGameMode's saved list
    // overrides this one, and Tools/setup_ship_parts.py writes that list;
    // DeepSpace.Ship.Parts.StockIsToday holds the two equal.
    const TCHAR* DefaultModulePaths[] = {
        TEXT("/Game/Ship/Parts/DA_Reactor_Stock.DA_Reactor_Stock"),
        TEXT("/Game/Ship/Parts/DA_Drive_Stock.DA_Drive_Stock"),
        TEXT("/Game/Ship/Parts/DA_Boosters_Stock.DA_Boosters_Stock"),
        TEXT("/Game/Ship/Parts/DA_Lights_Stock.DA_Lights_Stock"),
        TEXT("/Game/Ship/Parts/DA_LifeSupport_Stock.DA_LifeSupport_Stock"),
        TEXT("/Game/Ship/Parts/DA_Sensors_Stock.DA_Sensors_Stock"),
    };
```

In `BeginPlay`, change the no-ship log to `TEXT("DeepSpaceGameMode: no ship subsystem; parts not fitted.")`. Then replace the loop with:

```cpp
    for (const TSoftObjectPtr<UShipModuleDataAsset>& SoftPart : StartingModules)
    {
        UShipModuleDataAsset* Part = SoftPart.LoadSynchronous();
        if (!Part)
        {
            UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: could not load part %s."), *SoftPart.ToString());
            continue;
        }
        if (!Ship->FitPart(Part))
        {
            UE_LOG(LogTemp, Warning, TEXT("DeepSpaceGameMode: part %s was refused (no bay, or no id?)."),
                   *Part->ModuleId.ToString());
        }
    }
```

In `Source/DeepSpace/Core/DeepSpaceGameMode.h`, replace the `StartingModules` doc comment with:

```cpp
    /**
     * The parts the ship starts with, one per core bay (wear and upgrades
     * decision 2), fitted through UShipSubsystem::FitPart. Data, not code:
     * Tools/setup_ship_parts.py writes the Blueprint's list, which overrides
     * this one. Soft pointers so an unloaded part costs nothing until the
     * level starts.
     */
```

- [ ] **Step 5: StockShip fits by bay**

Replace `Source/DeepSpace/Tests/StockShip.h` with:

```cpp
#pragma once

#include "Core/DeepSpaceGameMode.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"
#include "UObject/SoftObjectPtr.h"

// The loadout play fits, for tests about what the power split does.
//
// A test world has no game mode, so a ship in one has every bay empty: stock
// ratings and nothing drawing off the top -- far more headroom than the ship
// anybody flies. Three times a claim about the split held there and not in
// play: the jump could never wind at full speed, the lights sat at 63% on a
// quiet ship, and after the reactor was resized the split stopped doing
// anything at all. Any test that asserts what a split, a draw or a lean
// does should run on this.
namespace StockShip
{
    /** The Blueprint game mode's starting parts -- what play uses, which may
     *  override the C++ list -- falling back to C++. */
    inline TArray<UShipModuleDataAsset*> Modules()
    {
        const UClass* ModeClass = LoadClass<ADeepSpaceGameMode>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceGameMode.BP_DeepSpaceGameMode_C"));
        const ADeepSpaceGameMode* Mode = ModeClass
            ? ModeClass->GetDefaultObject<ADeepSpaceGameMode>()
            : GetDefault<ADeepSpaceGameMode>();
        TArray<UShipModuleDataAsset*> Loaded;
        for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : Mode->GetStartingModules())
        {
            if (UShipModuleDataAsset* Module = Soft.LoadSynchronous())
            {
                Loaded.Add(Module);
            }
        }
        return Loaded;
    }

    /** Fits the stock loadout, one part per core bay; returns how many went in. */
    inline int32 Install(UShipSubsystem* Ship)
    {
        int32 Fitted = 0;
        for (UShipModuleDataAsset* Part : Modules())
        {
            Fitted += Ship->FitPart(Part) ? 1 : 0;
        }
        return Fitted;
    }
}
```

- [ ] **Step 6: Retire InstallModule and RemoveModule**

In `Source/DeepSpace/Ship/ShipSubsystem.h`, delete the `InstallModule` declaration with its comment and `UFUNCTION`, and do the same for `RemoveModule`. Delete the inline `GetInstalledModules` with its comment. Delete the private `UPROPERTY() TArray<TObjectPtr<UShipModuleDataAsset>> InstalledModules;`. Then add, before the `// -- parts: one per bay` block:

```cpp
    /** Every fitted part, in bay order: a read-only view, for tests. The
     *  loadout itself is GetLoadoutState. */
    TArray<UShipModuleDataAsset*> GetInstalledModules() const;
```

In `Source/DeepSpace/Ship/ShipSubsystem.cpp`, delete the definitions of `UShipSubsystem::InstallModule` and `UShipSubsystem::RemoveModule`. Then add, after `GetFittedPart`'s definition:

```cpp
TArray<UShipModuleDataAsset*> UShipSubsystem::GetInstalledModules() const
{
    TArray<UShipModuleDataAsset*> Fitted;
    for (const EShipBay Bay : ShipBay::All())
    {
        if (UShipModuleDataAsset* Part = GetFittedPart(Bay))
        {
            Fitted.Add(Part);
        }
    }
    return Fitted;
}
```

- [ ] **Step 7: The tests that installed modules fit parts; the hogs are loads**

Each keeps its numbers:
- `Tests/JumpWindsTest.cpp:85`: `TestTrue(FString::Printf(TEXT("%s installs"), *Module->GetName()), Ship->InstallModule(Module));` becomes `TestTrue(FString::Printf(TEXT("%s fits"), *Module->GetName()), Ship->FitPart(Module));`.
- `Tests/JumpWindsTest.cpp:152`: `Default->InstallModule(Module);` becomes `Default->FitPart(Module);`.
- `Tests/SliceChooseTest.cpp:451`: `Test.Ship->InstallModule(Module);` becomes `Test.Ship->FitPart(Module);`.
- `Tests/LampPanelsDimTest.cpp`: replace the lines from `UShipModuleDataAsset* Load = NewObject<UShipModuleDataAsset>();` to `TestTrue(TEXT("the load installs"), Ship->InstallModule(Load));` with:

```cpp
    // A test load, not a part (wear decision 8): a standing draw is exactly
    // what no part may have.
    Ship->SetConsumerWeight(ShipPower::Lights, 1.0f);
    Ship->AddLoad(TEXT("Test.Load"), Ship->GetReactorOutput() - 100.0f);
```

  and replace `Ship->RemoveModule(Load);` with `Ship->RemoveLoad(TEXT("Test.Load"));`.
- `Tests/ShipSkyTest.cpp`: replace the lines from `UShipModuleDataAsset* Hog = NewObject<UShipModuleDataAsset>();` to `TestTrue(TEXT("a heavy module installs"), Ship->InstallModule(Hog));` with:

```cpp
            // Most of whatever the reactor has left after what is fitted, so
            // it stays "most of the headroom" on any loadout. A test load,
            // not a part (wear decision 8).
            float Installed = 0.0f;
            for (const UShipModuleDataAsset* Module : Ship->GetInstalledModules())
            {
                Installed += Module->PowerDraw;
            }
            Ship->AddLoad(TEXT("Test.VeilHog"), 0.8f * (Ship->GetReactorOutput() - Installed));
```

  and replace `Ship->RemoveModule(Hog);` with `Ship->RemoveLoad(TEXT("Test.VeilHog"));`.

- [ ] **Step 8: Run them and see them pass, and every test that moved**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.StockIsToday && \
./test.sh DeepSpace.Ship.Parts.RatingsFollowParts && ./test.sh DeepSpace.Ship.JumpCanWindAtFullSpeed && ./test.sh DeepSpace.Loop && \
./test.sh DeepSpace.Ship.LampPanelsDim && ./test.sh DeepSpace.Sky.ShipSky && ./test.sh DeepSpace.Sky.LiveShipSky && \
./test.sh DeepSpace.Ship.BrownOutRunsWarmer && ./test.sh DeepSpace.Ship.PowerAllocation && ./test.sh DeepSpace.Ship.PowerConsumers
```

Expected: every run `passed: N` with no `FAILED`, where N ≥ 1. Every `StockShip` test runs on six parts with the same 620 W off the top.

- [ ] **Step 9: No Blueprint called what was removed**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && . Tools/ue_lock.sh && \
ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_blueprints.py" -unattended -nopause -nosplash -NoLiveCoding; echo "exit $?"
```

Expected: `exit 0`. If a Blueprint called `InstallModule` or `RemoveModule`, it fails here. Stop and report it: ADR 0002 says no Blueprint should.

- [ ] **Step 10: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Source/DeepSpace/Core/DeepSpaceGameMode.h Source/DeepSpace/Core/DeepSpaceGameMode.cpp Source/DeepSpace/Tests/StockShip.h \
        Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/JumpWindsTest.cpp \
        Source/DeepSpace/Tests/SliceChooseTest.cpp Source/DeepSpace/Tests/LampPanelsDimTest.cpp Source/DeepSpace/Tests/ShipSkyTest.cpp \
        Source/DeepSpace/Tests/ShipLoadoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(ship): the stock ship is six parts; InstallModule and RemoveModule retired

The game mode and StockShip fit the six stock parts through FitPart.
JumpWinds and SliceChoose fit parts where they installed modules, and the
hog tests (LampPanelsDim, ShipSky) starve the ship through AddLoad: a
standing draw is what no part may have (decision 8, sign-off 28). Every
number is unchanged. GetInstalledModules stays, as a read-only view of the
bays.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 11: Prove each can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'PowerState.AddDraw(Key, Fitted->PowerDraw);' 'PowerState.AddDraw(Key, 0.0f);' DeepSpace.Ship.Parts.StockIsToday && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'PowerState.SetReactorOutput(static_cast<float>(Rated.ReactorWatts));' 'PowerState.SetReactorOutput(1400.0f);' DeepSpace.Ship.Parts.RatingsFollowParts && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'Limits.DriveResponse = GetDriveResponse();' 'Limits.DriveResponse = ShipParts::StockDriveResponse;' DeepSpace.Ship.Parts.RatingsFollowParts && \
./build.sh
```

Expected: `KILLED` three times, then `Result: Succeeded`:
1. 620 W is no longer drawn;
2. the twin core does not give 1800 W;
3. the quick lever's 4.5 never reaches the flight.

---

## Task 9 (S4): Nameplates on the engineering console; DRAWN and SPARE removed

**Owner:** S. **Depends on:** Task 8. Spec decision 9; sign-offs 10, 11, 12.

**Files:**
- Modify: `Source/DeepSpace/UI/EngineeringConsoleWidget.h` (whole class, lines 10-45), `Source/DeepSpace/UI/EngineeringConsoleWidget.cpp` (whole file)
- Modify: `Source/DeepSpace/Ship/ShipConsole.cpp` (`GetReadout`, lines 107-120; includes)
- Modify: `Source/DeepSpace/Tests/ShipScreensAgreeTest.cpp` (includes; lines 88-106)
- Modify: `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (append `DeepSpace.Ship.Parts.NameplatesAreFacts`)

**Interfaces:**
- Consumes: `GetFittedPart`, `AddLoad`, `RemoveLoad`, `FitPart`, `RemovePart` (S2, S3); `ShipBay::PlateLabel`, `ShipParts::Apply` (D1); `ShipPartsJson::*` (D3); the catalogue's assets (D4).
- Produces:
  - `FText UEngineeringConsoleWidget::GetReadoutText() const`: every nameplate, one line each;
  - `void RefreshFromShip()`, `FText GetShownText() const`;
  - `static FString Nameplate(EShipBay Bay, const UShipModuleDataAsset& Part)`, `static constexpr float PlateSize = 13.0f`.
  `AShipConsole::GetReadout()` keeps its signature and returns the reactor's nameplate.

- [ ] **Step 1: Write the failing test**

In `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`, add `#include "Tests/ShipPartsJson.h"` and `#include "UI/EngineeringConsoleWidget.h"` to the includes. Then append, before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsNameplatesAreFactsTest, "DeepSpace.Ship.Parts.NameplatesAreFacts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    template <typename TWidget>
    TWidget* MakeScreen(UWorld* World)
    {
        TWidget* Widget = NewObject<TWidget>(World);
        Widget->Initialize();
        Widget->TakeWidget();
        return Widget;
    }

    /** A plate's columns: runs of two or more spaces separate them. */
    TArray<FString> Columns(const FString& Line)
    {
        TArray<FString> Pieces;
        Line.ParseIntoArray(Pieces, TEXT("  "), true);
        TArray<FString> Out;
        for (FString& Piece : Pieces)
        {
            Piece.TrimStartAndEndInline();
            if (!Piece.IsEmpty())
            {
                Out.Add(Piece);
            }
        }
        return Out;
    }

    /** Every number in Text, as a value: digits, commas grouping them, one
     *  decimal point between digits. */
    TArray<double> Numbers(const FString& Text)
    {
        TArray<double> Found;
        FString Current;
        for (int32 Index = 0; Index <= Text.Len(); ++Index)
        {
            const TCHAR Char = Index < Text.Len() ? Text[Index] : TEXT(' ');
            const bool bJoin = !Current.IsEmpty() && (Char == TEXT(',') || Char == TEXT('.'))
                && Index + 1 < Text.Len() && FChar::IsDigit(Text[Index + 1]);
            if (FChar::IsDigit(Char) || bJoin)
            {
                if (Char != TEXT(','))
                {
                    Current.AppendChar(Char);
                }
            }
            else if (!Current.IsEmpty())
            {
                Found.Add(FCString::Atod(*Current));
                Current.Reset();
            }
        }
        return Found;
    }

    /** A word no plate may carry (decision 9), or empty. */
    FString Forbidden(const FString& Line)
    {
        if (Line.Contains(TEXT("%")))
        {
            return TEXT("%");
        }
        static const TCHAR* Words[] = {
            TEXT("mk"), TEXT("tier"), TEXT("ii"), TEXT("iii"), TEXT("iv"), TEXT("upgrade"), TEXT("upgraded"), TEXT("basic"),
            TEXT("stock"), TEXT("improved"), TEXT("better"), TEXT("best"), TEXT("standard"), TEXT("draw"), TEXT("drawn"),
            TEXT("spare"), TEXT("condition"), TEXT("worn"),
        };
        TArray<FString> Tokens;
        FString Token;
        for (int32 Index = 0; Index <= Line.Len(); ++Index)
        {
            const TCHAR Char = Index < Line.Len() ? Line[Index] : TEXT(' ');
            if (FChar::IsAlnum(Char))
            {
                Token.AppendChar(FChar::ToLower(Char));
            }
            else if (!Token.IsEmpty())
            {
                Tokens.Add(Token);
                Token.Reset();
            }
        }
        for (const TCHAR* Word : Words)
        {
            if (Tokens.Contains(Word))
            {
                return Word;
            }
        }
        return FString();
    }

    /** The one number decision 9's table says Part's plate carries, as
     *  printed (whole watts; tenths otherwise), and its unit. */
    double PlateFigure(EShipBay Bay, const UShipModuleDataAsset& Part, FString& Unit)
    {
        FShipRatings Rated = FShipRatings::Stock();
        ShipParts::Apply(Rated, Part.Ratings);
        const auto Tenths = [](double Value) { return FMath::RoundToDouble(10.0 * Value) / 10.0; };
        switch (Bay)
        {
        case EShipBay::Reactor:     Unit = TEXT("W"); return FMath::RoundToDouble(Rated.ReactorWatts);
        case EShipBay::Drive:       Unit = TEXT("notches/s"); return Tenths(Rated.DriveResponse);
        case EShipBay::Boosters:    Unit = TEXT("km/s²"); return Tenths(Rated.LinearAcceleration / 1.0e5);
        case EShipBay::Lights:      Unit = TEXT("W"); return FMath::RoundToDouble(Rated.LightsWant);
        case EShipBay::LifeSupport: Unit = TEXT("W"); return FMath::RoundToDouble(Part.PowerDraw);
        case EShipBay::Sensors:     Unit = TEXT("ly"); return Tenths(Rated.RangeLy);
        default:                    Unit.Reset(); return 0.0;
        }
    }

    void CheckPlate(FAutomationTestBase& Test, const FString& Line, EShipBay Bay, const UShipModuleDataAsset& Part, const TCHAR* When)
    {
        const FString Id = Part.ModuleId.ToString();
        const bool bAux = ShipBay::IsAux(Bay);
        const TArray<FString> Cols = Columns(Line);
        if (!Test.TestEqual(FString::Printf(TEXT("%s, %s: bay, name, %swords ('%s')"), When, *Id, bAux ? TEXT("") : TEXT("one figure, "), *Line),
                            Cols.Num(), bAux ? 3 : 4))
        {
            return;
        }
        Test.TestEqual(FString::Printf(TEXT("%s, %s: the bay"), When, *Id), Cols[0], ShipBay::PlateLabel(Bay));
        Test.TestEqual(FString::Printf(TEXT("%s, %s: its name"), When, *Id), Cols[1], Part.DisplayName.ToString());
        Test.TestEqual(FString::Printf(TEXT("%s, %s: its words"), When, *Id), Cols.Last(), Part.Words.ToString());
        const FString Word = Forbidden(Line);
        Test.TestTrue(FString::Printf(TEXT("%s, %s: no percentage, tier, comparison, total or condition (found '%s')"), When, *Id, *Word),
                      Word.IsEmpty());
        const TArray<double> InLine = Numbers(Line);
        if (bAux)
        {
            Test.TestEqual(FString::Printf(TEXT("%s, %s: an aux plate carries no number"), When, *Id), InLine.Num(), 0);
            return;
        }
        FString Unit;
        const double Want = PlateFigure(Bay, Part, Unit);
        Test.TestEqual(FString::Printf(TEXT("%s, %s: exactly one number on the line"), When, *Id), InLine.Num(), 1);
        Test.TestTrue(FString::Printf(TEXT("%s, %s: in %s ('%s')"), When, *Id, *Unit, *Cols[2]), Cols[2].EndsWith(TEXT(" ") + Unit));
        Test.TestTrue(FString::Printf(TEXT("%s, %s: the part's own %g"), When, *Id, Want),
                      InLine.Num() == 1 && FMath::IsNearlyEqual(InLine[0], Want, 1e-9));
    }
}

/*
 * Decision 9: one nameplate per fitted part, BAY Name figure Words, each
 * figure the part's own. Never a percentage, a tier, a comparison, a
 * condition or a symptom; no line for an empty slot; never the charge; and
 * no total drawn or headroom anywhere on the console (the lived-in spec's
 * decision 11). No console variable moves a plate.
 */
bool FShipPartsNameplatesAreFactsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("NameplatesWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();
    TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);
    Ship->Tick(0.01f);
    UEngineeringConsoleWidget* Console = MakeScreen<UEngineeringConsoleWidget>(Test.World);
    const auto Shown = [&]()
    {
        Console->RefreshFromShip();
        return Console->GetShownText().ToString();
    };
    const auto CheckScreen = [&](const TCHAR* When)
    {
        const FString Text = Shown();
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines);
        TArray<EShipBay> Fitted;
        for (const EShipBay Bay : ShipBay::All())
        {
            if (Ship->GetFittedPart(Bay))
            {
                Fitted.Add(Bay);
            }
        }
        if (!TestEqual(FString::Printf(TEXT("%s: one plate per fitted part, in bay order, and none for an empty slot"), When),
                       Lines.Num(), Fitted.Num()))
        {
            return;
        }
        for (int32 Index = 0; Index < Lines.Num(); ++Index)
        {
            CheckPlate(*this, Lines[Index], Fitted[Index], *Ship->GetFittedPart(Fitted[Index]), When);
        }
        TestFalse(FString::Printf(TEXT("%s: never the charge"), When), Numbers(Text).Contains(static_cast<double>(Ship->GetChargeSeconds())));
    };

    CheckScreen(TEXT("the stock ship"));

    // Every row of the catalogue, as its own plate.
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    for (const ShipPartsJson::FRow& Row : Catalogue.Rows)
    {
        const UShipModuleDataAsset* Part = LoadObject<UShipModuleDataAsset>(nullptr, *ShipPartsJson::ObjectPath(Catalogue, Row.Asset));
        if (TestNotNull(FString::Printf(TEXT("%s is authored"), *Row.Spec.Id.ToString()), Part))
        {
            CheckPlate(*this, UEngineeringConsoleWidget::Nameplate(Part->Bay, *Part), Part->Bay, *Part, TEXT("the catalogue"));
        }
    }

    // Each example upgrade, fitted.
    for (const TCHAR* Asset : { TEXT("DA_Reactor_TwinCore"), TEXT("DA_Drive_QuickLever") })
    {
        TestTrue(FString::Printf(TEXT("%s fits"), Asset), Ship->FitPart(LoadPart(Asset)));
        Ship->Tick(0.01f);
        CheckScreen(*FString::Printf(TEXT("with %s"), Asset));
    }

    // An aux part: its plate carries no number, and once it is stowed no line is left.
    TestTrue(TEXT("an aux part fits"), Ship->FitPart(MakePart(TEXT("Aux.Scope"), EShipBay::Aux1, 0.0f, {},
        TEXT("Long-focus telescope"), TEXT("Somebody scratched a chart into its hood."))));
    CheckScreen(TEXT("with an aux part"));
    TestTrue(TEXT("and is stowed"), Ship->RemovePart(EShipBay::Aux1));
    CheckScreen(TEXT("the aux part stowed"));

    // No total drawn and no headroom. A 13 W load makes both numbers no part
    // rates, so if either were printed it would be seen.
    Ship->AddLoad(TEXT("Test.Hog"), 13.0f);
    Ship->Tick(0.01f);
    const double Drawn = FMath::RoundToDouble(Ship->GetPowerDraw());
    const double Headroom = FMath::RoundToDouble(Ship->GetPowerHeadroom());
    TArray<double> Figures;
    for (const EShipBay Bay : ShipBay::All())
    {
        if (const UShipModuleDataAsset* Part = Ship->GetFittedPart(Bay))
        {
            FString Unit;
            Figures.Add(PlateFigure(Bay, *Part, Unit));
        }
    }
    AddInfo(FString::Printf(TEXT("drawn %.0f W, headroom %.0f W"), Drawn, Headroom));
    TestTrue(TEXT("the load makes the draw and the headroom numbers no plate carries"),
             !Figures.Contains(Drawn) && !Figures.Contains(Headroom));
    const TArray<double> OnScreen = Numbers(Shown());
    TestFalse(TEXT("no total drawn on the console"), OnScreen.Contains(Drawn));
    TestFalse(TEXT("and no headroom"), OnScreen.Contains(Headroom));
    TestTrue(TEXT("the load comes off"), Ship->RemoveLoad(TEXT("Test.Hog")));

    // No console variable moves a plate.
    const FString Before = Shown();
    {
        FScopedCVar Range(TEXT("ds.Nav.RangeLy"), 15.0f);
        FScopedCVar Response(TEXT("ds.Drive.Response"), 1.0f);
        FScopedCVar Top(TEXT("ds.Drive.Top"), 0.05f);
        Ship->Tick(0.01f);
        TestEqual(TEXT("ds.Nav.RangeLy 15, ds.Drive.Response 1 and ds.Drive.Top 0.05 change no plate"), Shown(), Before);
    }
    Ship->Tick(0.01f);
    return true;
}
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh
```

Expected: the build fails, `no member named 'RefreshFromShip' in 'UEngineeringConsoleWidget'`.

- [ ] **Step 3: The widget's plates, in the header**

In `Source/DeepSpace/UI/EngineeringConsoleWidget.h`, add `#include "Ship/ShipParts.h"` after `#include "UI/ShipScreenWidget.h"`, and `class UShipModuleDataAsset;` beside the other forward declarations. Then replace the class comment and the class with:

```cpp
/**
 * The engineering console's screen: the light switch, and one nameplate
 * per fitted part (wear and upgrades decision 9).
 *
 * Figures, never judgements. Each plate is BAY, the part's name, the one
 * number its bay's row names -- read from the part itself, never from a
 * console variable, the flight's limits, the live power split or wear -- and
 * its words, which are character and history, never quality. Nothing here
 * totals, compares or ranks: no percentage, no tier, no "upgraded", no total
 * drawn and no headroom (the lived-in spec's decision 11), and no line for
 * an empty slot, which would be a gap to fill. A screen that invents an
 * optimum turns living with the ship into a puzzle with an answer
 * (docs/vision.md, the anti-chore principle).
 */
UCLASS()
class DEEPSPACE_API UEngineeringConsoleWidget : public UShipScreenWidget
{
    GENERATED_BODY()

public:
    /** The screen's light switch. Public because a test drives it, and
     *  because it is exactly what the console's E key does. */
    UFUNCTION()
    void ToggleLights();

    /** Every fitted part's nameplate, one line each, in bay order. */
    FText GetReadoutText() const;

    /** Puts the plates and the switch's words on the screen: what NativeTick
     *  does every frame, public so a test renders without painting. */
    void RefreshFromShip();

    /** What the plates' text block shows, as last refreshed. */
    FText GetShownText() const;

    /** One part's plate: its bay's label, its name, its one figure and unit
     *  (none for an aux part), its words, in columns two spaces apart. */
    static FString Nameplate(EShipBay Bay, const UShipModuleDataAsset& Part);

    /** Six plates across a 600-pixel panel: smaller than the body text.
     *  Whether it reads at the console is a playtest question. */
    static constexpr float PlateSize = 13.0f;

protected:
    virtual UWidget* BuildScreen() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    UPROPERTY()
    TObjectPtr<UTextBlock> LightsLabel;

    UPROPERTY()
    TObjectPtr<UTextBlock> Readout;
};
```

- [ ] **Step 4: The widget's plates, in the implementation**

Replace `Source/DeepSpace/UI/EngineeringConsoleWidget.cpp` with:

```cpp
#include "UI/EngineeringConsoleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Ship/ShipModuleDataAsset.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/UniverseUnits.h"

UWidget* UEngineeringConsoleWidget::BuildScreen()
{
    UVerticalBox* Column = MakeColumn();

    Column->AddChildToVerticalBox(MakeText(
        NSLOCTEXT("DeepSpace", "ConsoleTitle", "ENGINEERING"), TitleSize, Accent));

    Readout = MakeText(FText::GetEmpty(), PlateSize, Ink);
    UVerticalBoxSlot* ReadoutSlot = Column->AddChildToVerticalBox(Readout);
    ReadoutSlot->SetPadding(FMargin(0.0f, 16.0f, 0.0f, 24.0f));

    UButton* Switch = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
    Switch->OnClicked.AddDynamic(this, &UEngineeringConsoleWidget::ToggleLights);
    LightsLabel = MakeText(FText::GetEmpty(), BodySize, Ink);
    Switch->SetContent(LightsLabel);
    Column->AddChildToVerticalBox(Switch);

    return MakePanel(Column);
}

void UEngineeringConsoleWidget::ToggleLights()
{
    if (UShipSubsystem* Subsystem = Ship())
    {
        Subsystem->SetLightsOn(!Subsystem->AreLightsOn());
    }
}

FString UEngineeringConsoleWidget::Nameplate(EShipBay Bay, const UShipModuleDataAsset& Part)
{
    // The part's own numbers, over stock for any it lacks (decision 3) --
    // never the ship's, which a console variable or wear could move.
    FShipRatings Rated = FShipRatings::Stock();
    ShipParts::Apply(Rated, Part.Ratings);
    FNumberFormattingOptions Tenths;
    Tenths.SetMaximumFractionalDigits(1);
    const auto Watts = [](double Value) { return FText::AsNumber(FMath::RoundToInt(Value)).ToString() + TEXT(" W"); };

    FString Figure;
    switch (Bay)
    {
    case EShipBay::Reactor:     Figure = Watts(Rated.ReactorWatts); break;
    case EShipBay::Drive:       Figure = FText::AsNumber(Rated.DriveResponse, &Tenths).ToString() + TEXT(" notches/s"); break;
    case EShipBay::Boosters:    Figure = FText::AsNumber(Rated.LinearAcceleration / UniverseUnits::CmPerKm, &Tenths).ToString() + TEXT(" km/s²"); break;
    case EShipBay::Lights:      Figure = Watts(Rated.LightsWant); break;
    case EShipBay::LifeSupport: Figure = Watts(Part.PowerDraw); break;
    case EShipBay::Sensors:     Figure = FText::AsNumber(Rated.RangeLy, &Tenths).ToString() + TEXT(" ly"); break;
    default:                    break;
    }

    const FString Label = ShipBay::PlateLabel(Bay);
    const FString Name = Part.DisplayName.ToString();
    const FString Words = Part.Words.ToString();
    return Figure.IsEmpty()
        ? FString::Printf(TEXT("%-12s  %-24s  %s"), *Label, *Name, *Words)
        : FString::Printf(TEXT("%-12s  %-24s  %-14s  %s"), *Label, *Name, *Figure, *Words);
}

FText UEngineeringConsoleWidget::GetReadoutText() const
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return FText::GetEmpty();
    }

    // Read live, from the authority. The screen holds no copy: which part is
    // in which bay is the ship's to say, every frame.
    TArray<FString> Lines;
    for (const EShipBay Bay : ShipBay::All())
    {
        if (const UShipModuleDataAsset* Part = Subsystem->GetFittedPart(Bay))
        {
            Lines.Add(Nameplate(Bay, *Part));
        }
    }
    return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

void UEngineeringConsoleWidget::RefreshFromShip()
{
    const UShipSubsystem* Subsystem = Ship();
    if (!Subsystem)
    {
        return;
    }
    if (Readout)
    {
        Readout->SetText(GetReadoutText());
    }
    if (LightsLabel)
    {
        LightsLabel->SetText(Subsystem->AreLightsOn()
            ? NSLOCTEXT("DeepSpace", "LightsOff", "  LIGHTS OFF  ")
            : NSLOCTEXT("DeepSpace", "LightsOn", "  LIGHTS ON  "));
    }
}

FText UEngineeringConsoleWidget::GetShownText() const
{
    return Readout ? Readout->GetText() : FText::GetEmpty();
}

void UEngineeringConsoleWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
    Super::NativeTick(Geometry, DeltaTime);
    RefreshFromShip();
}
```

- [ ] **Step 5: The console actor's readout is the reactor's plate**

In `Source/DeepSpace/Ship/ShipConsole.cpp`, add `#include "Ship/ShipModuleDataAsset.h"`. Then replace the body of `AShipConsole::GetReadout` after the `NO SIGNAL` return with:

```cpp
    // The reactor's nameplate, the first line of the console's list (wear
    // decision 9): never a total drawn or a headroom, which the lived-in
    // spec's decision 11 rules out. The signature stays, because
    // BP_ShipConsole may still call it.
    const UShipModuleDataAsset* Reactor = Ship->GetFittedPart(EShipBay::Reactor);
    return Reactor ? FText::FromString(UEngineeringConsoleWidget::Nameplate(EShipBay::Reactor, *Reactor)) : FText::GetEmpty();
```

- [ ] **Step 6: ScreensAgree checks the laptop's lights row, and the console's plates hold still**

In `Source/DeepSpace/Tests/ShipScreensAgreeTest.cpp`, add `#include "Tests/StockShip.h"`. Replace the block from `// Two different *kinds* of screen agree too: the console's switch` through the `TestNotEqual(TEXT("and the readout changed with it"), ...);` statement with:

```cpp
        // Two different *kinds* of screen agree too: the console's switch
        // and the laptop's readout are views of one value. On the stock
        // ship, so the console has plates to show.
        TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);
        UEngineeringConsoleWidget* Console = MakeScreen<UEngineeringConsoleWidget>(World);
        TestTrue(TEXT("the ship starts lit"), Ship->AreLightsOn());
        const FString Plates = Console->GetReadoutText().ToString();
        TestFalse(TEXT("the console shows its plates"), Plates.IsEmpty());

        Console->ToggleLights();
        TestFalse(TEXT("the console's switch reaches the ship"), Ship->AreLightsOn());

        Galley->RefreshFromShip();
        const FString Dark = Galley->GetRowText(ShipPower::Lights).ToString();
        TestTrue(TEXT("and the laptop shows the lights wanting nothing"), Dark.Contains(TEXT("0 W of 0 W")));

        // Recomputed, never remembered: the freed power shows up on the
        // laptop's lights row as it is reallocated. The console shows
        // nameplates, which the switch does not move (wear decision 9).
        Console->ToggleLights();
        TestTrue(TEXT("the ship is lit again"), Ship->AreLightsOn());
        Galley->RefreshFromShip();
        TestNotEqual(TEXT("and the laptop's lights row changed with it"), Galley->GetRowText(ShipPower::Lights).ToString(), Dark);
        TestEqual(TEXT("while the console's plates did not"), Console->GetReadoutText().ToString(), Plates);
```

- [ ] **Step 7: Run it and see it pass, and ScreensAgree**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.NameplatesAreFacts && \
./test.sh DeepSpace.Ship.ScreensAgree && ./test.sh DeepSpace.Ship.ChartChair && ./test.sh DeepSpace.Ship.ScreenReachable
```

Expected: `passed: N` each time with no `FAILED`, where N ≥ 1. `ChartChair` and `ScreenReachable` spawn an `AShipConsole`.

- [ ] **Step 8: The console Blueprint still compiles**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && . Tools/ue_lock.sh && \
ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_blueprints.py" -unattended -nopause -nosplash -NoLiveCoding; echo "exit $?"
```

Expected: `exit 0` (`BP_ShipConsole` calls `GetReadout`, whose signature is unchanged).

- [ ] **Step 9: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Source/DeepSpace/UI/EngineeringConsoleWidget.h Source/DeepSpace/UI/EngineeringConsoleWidget.cpp \
        Source/DeepSpace/Ship/ShipConsole.cpp Source/DeepSpace/Tests/ShipScreensAgreeTest.cpp Source/DeepSpace/Tests/ShipLoadoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(console): a nameplate per fitted part; DRAWN and SPARE removed

The engineering console lists BAY, name, the part's one figure and its
words, read from the part, in bay order, with no line for an empty slot.
The REACTOR / DRAWN / SPARE readout predates the lived-in spec's decision
11 ("nothing anywhere in the ship shows a total drawn") and goes; the
reactor's line is its nameplate, which AShipConsole::GetReadout returns.
ScreensAgree's "the readout changed" check moves to the laptop's lights
row, and the console's plates are asserted unchanged by the switch.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 10: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/UI/EngineeringConsoleWidget.cpp 'case EShipBay::Reactor:     Figure = Watts(Rated.ReactorWatts); break;' 'case EShipBay::Reactor:     Figure = Watts(Rated.ReactorWatts - 1.0); break;' DeepSpace.Ship.Parts.NameplatesAreFacts && \
Tools/mutate.sh Source/DeepSpace/UI/EngineeringConsoleWidget.cpp 'case EShipBay::Drive:       Figure = FText::AsNumber(Rated.DriveResponse, &Tenths).ToString() + TEXT(" notches/s"); break;' 'case EShipBay::Drive:       Figure = FText::AsNumber(Rated.DriveResponse, &Tenths).ToString() + TEXT(" notches/s, 45 s"); break;' DeepSpace.Ship.Parts.NameplatesAreFacts && \
./build.sh
```

Expected: `KILLED` twice, then `Result: Succeeded`:
1. the reactor plate reads 1,399 W;
2. the drive's plate carries a second number, and the charge.

---

## Task 10 (S5): The catalogue asset, FindPart, and fitting by id

**Owner:** S. **Depends on:** Task 9, and **the gate**: `feat/landing-a-procgen` merged to `main` (it has, as `d1cfbc8`, and the branch is deleted), and the atmosphere plan's Task 9 (G2) not mid-edit on `Config/DefaultGame.ini` in `.worktrees/air-procgen` (*Execution order*, the atmosphere table). Spec decision 5; decision 10 (the first spare with that id, else a new one).

**Files:**
- Modify: `Config/DefaultGame.ini` (append one section at the end of the file)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (`UCLASS()` at line 47 becomes `UCLASS(Config = Game)`; public: `FindPart`, `GetCatalogue`, `FitPartById` in the parts block; private: `CatalogueAsset`, `PartFor`)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` (include `Ship/ShipPartCatalogue.h`; the new definitions after `GetInstalledModules`)
- Modify: `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (append `DeepSpace.Ship.Parts.CatalogueResolves`)

**Interfaces:**
- Consumes: `UShipPartCatalogue` and `DA_ShipCatalogue` (D2, D4); `Register`, `SlotFor`, `FitState`, `FitPart` (S2).
- Produces: `UShipModuleDataAsset* UShipSubsystem::FindPart(FName PartId) const`, `TArray<UShipModuleDataAsset*> GetCatalogue() const`, `bool FitPartById(FName PartId)`; private `UShipModuleDataAsset* PartFor(FName PartId) const` (a known part, else the catalogue's); `UPROPERTY(Config) FSoftObjectPath CatalogueAsset`.

- [ ] **Step 1: The gate, then bring main in**

```bash
cd /home/matt/Development/deepspace && \
git log --oneline -1 --fixed-strings --grep='merge: landing-a-procgen' main | grep -q . && echo "P merged" && \
{ [ ! -d .worktrees/air-procgen ] || [ -z "$(git -C .worktrees/air-procgen status --short -- Config/DefaultGame.ini)" ]; } && \
echo "air-procgen not editing the ini" && \
git -C .worktrees/wear-1-s merge -q --no-edit main && git -C .worktrees/wear-1-s log --oneline -1
```

Expected: `P merged`, `air-procgen not editing the ini`, then a clean merge. The first check looks for the merge commit, not the branch, because `feat/landing-a-procgen` was deleted once merged. If it exits 1, landing's procgen track has not merged: wait for it. If the second exits 1, the atmosphere plan's G2 holds an uncommitted edit to `Config/DefaultGame.ini`: wait until it commits, then run this step again. G2's air lines sit inside `[/Script/DeepSpace.ProcGenPriorsConfig]` and this task's section goes at the end of the file, so the two branches merge cleanly in either order once neither is mid-edit. Do not edit `Config/DefaultGame.ini` until both checks pass. After the merge, run `./build.sh` in `wear-1-s`: landing (a) changed `Universe/` and `Sky/SkySystem.*`.

- [ ] **Step 2: Write the failing test**

Append to `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`, before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCatalogueResolvesTest, "DeepSpace.Ship.Parts.CatalogueResolves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Decision 5: in a bare world, with no game mode and no asset-registry
 * scan, every id in Tools/ship_parts.json resolves through FindPart, and
 * nothing else does. By id, the first spare with that id is fitted before a
 * new one is made (decision 10), and the part a bay already holds is left
 * alone (review focus 2).
 */
bool FShipPartsCatalogueResolvesTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("CatalogueResolvesWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    const ShipPartsJson::FCatalogue Catalogue = ShipPartsJson::Read();
    TArray<FString> JsonIds;
    for (const ShipPartsJson::FRow& Row : Catalogue.Rows)
    {
        JsonIds.Add(Row.Spec.Id.ToString());
        const UShipModuleDataAsset* Part = Ship->FindPart(Row.Spec.Id);
        if (TestNotNull(FString::Printf(TEXT("%s resolves"), *Row.Spec.Id.ToString()), Part))
        {
            TestEqual(FString::Printf(TEXT("%s is its own asset"), *Row.Spec.Id.ToString()),
                      FSoftObjectPath(Part).ToString(), ShipPartsJson::ObjectPath(Catalogue, Row.Asset));
        }
    }
    TArray<FString> CatalogueIds;
    for (const UShipModuleDataAsset* Part : Ship->GetCatalogue())
    {
        CatalogueIds.Add(Part->ModuleId.ToString());
    }
    TestEqual(TEXT("the catalogue is the JSON's rows, in order, and nothing else"),
              FString::Join(CatalogueIds, TEXT(",")), FString::Join(JsonIds, TEXT(",")));
    TestNull(TEXT("an id the JSON does not have does not resolve"), Ship->FindPart(TEXT("Reactor.Nonesuch")));
    TestNull(TEXT("and neither does no id"), Ship->FindPart(NAME_None));

    TestTrue(TEXT("the twin core fits by id"), Ship->FitPartById(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("and the supply is 1800 W"), Ship->GetReactorOutput(), 1800.0f);
    TestEqual(TEXT("an empty bay displaced nothing"), SpareIds(*Ship), FString());
    TestFalse(TEXT("an unknown id fits nothing"), Ship->FitPartById(TEXT("Reactor.Nonesuch")));
    TestTrue(TEXT("and changes nothing"), Ship->GetReactorOutput() == 1800.0f && Ship->GetSpares().IsEmpty());

    TestTrue(TEXT("the twin core by id again is taken"), Ship->FitPartById(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("and makes no spare of itself"), SpareIds(*Ship), FString());

    TestTrue(TEXT("the stock reactor by id"), Ship->FitPartById(TEXT("Reactor.Stock")));
    TestEqual(TEXT("puts the twin core in the spares"), SpareIds(*Ship), FString(TEXT("Reactor.TwinCore")));
    TestTrue(TEXT("and the twin core by id"), Ship->FitPartById(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("comes out of the spares, never conjured anew"), SpareIds(*Ship), FString(TEXT("Reactor.Stock")));
    TestEqual(TEXT("and runs the ship"), Ship->GetReactorOutput(), 1800.0f);
    return true;
}
```

- [ ] **Step 3: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh
```

Expected: the build fails, `no member named 'FindPart' in 'UShipSubsystem'`.

- [ ] **Step 4: The ini line**

Append to `Config/DefaultGame.ini`, after the last line:

```ini

[/Script/DeepSpace.ShipSubsystem]
; Every part there is, as Tools/setup_ship_parts.py writes it from
; Tools/ship_parts.json (wear and upgrades decision 5). UShipSubsystem::FindPart
; loads it on first ask. The C++ default is empty on purpose: a section that
; fails to load finds no part at all rather than passing for the right
; catalogue, and DeepSpace.Ship.Parts.CatalogueResolves says so.
CatalogueAsset=/Game/Ship/Parts/DA_ShipCatalogue.DA_ShipCatalogue
```

- [ ] **Step 5: FindPart and fitting by id**

In `Source/DeepSpace/Ship/ShipSubsystem.h`, change `UCLASS()` above `class DEEPSPACE_API UShipSubsystem` to `UCLASS(Config = Game)`. In the public parts block, after `RemovePart`, add:

```cpp
    /** The catalogue's part with this id, loaded on first ask through the
     *  CatalogueAsset ini line (decision 5); null for an id it does not
     *  hold. Works in a bare world with no game mode and no asset scan. */
    UShipModuleDataAsset* FindPart(FName PartId) const;

    /** Every part the catalogue holds, in its order. */
    TArray<UShipModuleDataAsset*> GetCatalogue() const;

    /**
     * ds.Ship.Install's path (decision 10): the first spare with this id,
     * fitted as it is, else a new one from the catalogue (FitPart). True with
     * nothing changed when the bay already holds it and no spare of it is
     * aboard; false for an id that names no part.
     */
    bool FitPartById(FName PartId);
```

In the private section, after `KnownParts`, add:

```cpp
    /** The catalogue, as DefaultGame.ini names it. Empty by default, on
     *  purpose: a section that fails to load finds nothing rather than the
     *  wrong catalogue. */
    UPROPERTY(Config)
    FSoftObjectPath CatalogueAsset;

    /** A part this ship already knows by id, else the catalogue's. */
    UShipModuleDataAsset* PartFor(FName PartId) const;
```

In `Source/DeepSpace/Ship/ShipSubsystem.cpp`, add `#include "Ship/ShipPartCatalogue.h"`. Then add, after `GetInstalledModules`'s definition:

```cpp
TArray<UShipModuleDataAsset*> UShipSubsystem::GetCatalogue() const
{
    TArray<UShipModuleDataAsset*> Parts;
    const UShipPartCatalogue* Catalogue = Cast<UShipPartCatalogue>(CatalogueAsset.TryLoad());
    if (!Catalogue)
    {
        return Parts;
    }
    for (const TSoftObjectPtr<UShipModuleDataAsset>& Soft : Catalogue->Parts)
    {
        if (UShipModuleDataAsset* Part = Soft.LoadSynchronous())
        {
            Parts.Add(Part);
        }
    }
    return Parts;
}

UShipModuleDataAsset* UShipSubsystem::FindPart(FName PartId) const
{
    if (PartId.IsNone())
    {
        return nullptr;
    }
    for (UShipModuleDataAsset* Part : GetCatalogue())
    {
        if (Part->ModuleId == PartId)
        {
            return Part;
        }
    }
    return nullptr;
}

UShipModuleDataAsset* UShipSubsystem::PartFor(FName PartId) const
{
    if (const TObjectPtr<UShipModuleDataAsset>* Known = KnownParts.Find(PartId))
    {
        return Known->Get();
    }
    return FindPart(PartId);
}

bool UShipSubsystem::FitPartById(FName PartId)
{
    UShipModuleDataAsset* Part = PartFor(PartId);
    if (!Part || !Register(Part))
    {
        return false;
    }
    const TOptional<EShipBay> Slot = SlotFor(*Part);
    if (!Slot)
    {
        return false;
    }
    const int32 Spare = Loadout.Spares.IndexOfByPredicate([PartId](const FShipPartState& State) { return State.PartId == PartId; });
    if (Spare == INDEX_NONE)
    {
        return FitPart(Part);
    }
    // A spare is one particular part, fitted as it is (decision 10): it keeps
    // its own state, and the part it displaces keeps its.
    const FShipPartState State = Loadout.Spares[Spare];
    Loadout.Spares.RemoveAt(Spare);
    FitState(*Slot, State);
    return true;
}
```

- [ ] **Step 6: Run it and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.CatalogueResolves && ./test.sh DeepSpace.Ship.Parts.OnePerBay
```

Expected: `passed: 1` both times.

- [ ] **Step 7: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Config/DefaultGame.ini Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipLoadoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(ship): FindPart through the catalogue asset the ini names; fitting by id takes a spare first

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 8: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'if (Part->ModuleId == PartId)' 'if (Part->ModuleId != PartId)' DeepSpace.Ship.Parts.CatalogueResolves && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '    if (Spare == INDEX_NONE)
    {
        return FitPart(Part);' '    if (true)
    {
        return FitPart(Part);' DeepSpace.Ship.Parts.CatalogueResolves && \
./build.sh
```

Expected: `KILLED` twice, then `Result: Succeeded`:
1. ids resolve to the wrong part;
2. the twin core is conjured anew, and the spares hold two stock reactors.

---

## Task 11 (S6): The console commands -- ds.Ship.Install, ds.Ship.Spares, ds.Ship.Describe

**Owner:** S. **Depends on:** Task 10. Spec decision 10; ruling 8.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public parts block: `AddSpare`, `ClearSpares`)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp`: the anonymous namespace (new commands after `NavChargeCommand`, about line 357); `AddSpare` and `ClearSpares` after `FitPartById`
- Modify: `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (append `DeepSpace.Ship.Parts.Commands`)

**Interfaces:**
- Consumes: `GetCatalogue`, `FitPartById`, `PartFor` (S5); `GetFittedPart`, `GetSpares`, `Register` (S2).
- Produces:
  - `bool UShipSubsystem::AddSpare(FName PartId)`, `void ClearSpares()`;
  - `ds.Ship.Install <part>`: an id or a display name, case-blind, the args joined by spaces;
  - `ds.Ship.Spares`, `ds.Ship.Spares give <part>`, `ds.Ship.Spares clear`;
  - `ds.Ship.Describe`.

- [ ] **Step 1: Write the failing test**

In `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`, add `#include "Misc/OutputDevice.h"` to the includes. Then append, before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsCommandsTest, "DeepSpace.Ship.Parts.Commands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShipLoadoutTestLocal
{
    /** What a console command printed. */
    struct FHeard : public FOutputDevice
    {
        FString Text;

        virtual void Serialize(const TCHAR* Line, ELogVerbosity::Type Verbosity, const FName& Category) override
        {
            Text += Line;
            Text += TEXT("\n");
        }
    };

    FString Run(UWorld* World, const TCHAR* Command, const TArray<FString>& Args)
    {
        FHeard Heard;
        if (IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(Command))
        {
            Object->AsCommand()->Execute(Args, World, Heard);
        }
        else
        {
            Heard.Text = FString::Printf(TEXT("no command %s"), Command);
        }
        return Heard.Text;
    }
}

/*
 * Ruling 8: until landing and a sourcing spec exist, parts come from the
 * console. ds.Ship.Install fits the first spare with that id, else a new
 * one, and each install restores its own bay's number (slice 1's done-when).
 */
bool FShipPartsCommandsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    FSkyWorld Test(TEXT("ShipCommandsWorld"));
    UShipSubsystem* Ship = Test.Ship;
    if (!TestNotNull(TEXT("the world has a ship"), Ship))
    {
        return false;
    }
    Test.BeginPlay();
    TestEqual(TEXT("the stock ship fits"), StockShip::Install(Ship), 6);
    UWorld* World = Test.World;

    Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.TwinCore") });
    TestEqual(TEXT("ds.Ship.Install Reactor.TwinCore: 1800 W"), Ship->GetReactorOutput(), 1800.0f);
    TestEqual(TEXT("and the stock reactor is a spare"), SpareIds(*Ship), FString(TEXT("Reactor.Stock")));
    const float Response = Ship->GetDriveResponse();

    Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.Stock") });
    TestEqual(TEXT("ds.Ship.Install Reactor.Stock: 1400 W again"), Ship->GetReactorOutput(), 1400.0f);
    TestEqual(TEXT("the stock reactor came back from the spares, and the twin core went in"), SpareIds(*Ship), FString(TEXT("Reactor.TwinCore")));
    TestEqual(TEXT("and the drive's number is its own"), Ship->GetDriveResponse(), Response);

    Run(World, TEXT("ds.Ship.Install"), { TEXT("Drive.QuickLever") });
    TestEqual(TEXT("ds.Ship.Install Drive.QuickLever: 4.5 notches a second"), Ship->GetDriveResponse(), 4.5f);
    TestEqual(TEXT("and the reactor's number is its own"), Ship->GetReactorOutput(), 1400.0f);
    Run(World, TEXT("ds.Ship.Install"), { TEXT("Drive.Stock") });
    TestEqual(TEXT("ds.Ship.Install Drive.Stock: 3 again"), Ship->GetDriveResponse(), 3.0f);

    Run(World, TEXT("ds.Ship.Install"), { TEXT("twin-core"), TEXT("REACTOR") });
    TestEqual(TEXT("by display name, case-blind, split on spaces: the twin core"), Ship->GetReactorOutput(), 1800.0f);
    TestEqual(TEXT("out of the spares"), SpareIds(*Ship), FString(TEXT("Drive.QuickLever,Reactor.Stock")));

    // Review focus 2, by id: the part the bay already holds, with no spare of it.
    Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.TwinCore") });
    TestEqual(TEXT("installing the fitted part changes no spare"), SpareIds(*Ship), FString(TEXT("Drive.QuickLever,Reactor.Stock")));
    TestEqual(TEXT("and no number"), Ship->GetReactorOutput(), 1800.0f);

    const FString Unknown = Run(World, TEXT("ds.Ship.Install"), { TEXT("Reactor.Nonesuch") });
    TestTrue(TEXT("an unknown part prints the usage"), Unknown.Contains(TEXT("ds.Ship.Install <part>")));
    TestTrue(TEXT("and names the parts there are"), Unknown.Contains(TEXT("Drive.QuickLever")));
    TestTrue(TEXT("and changes nothing"),
             Ship->GetReactorOutput() == 1800.0f && SpareIds(*Ship) == TEXT("Drive.QuickLever,Reactor.Stock"));

    const FString Listed = Run(World, TEXT("ds.Ship.Spares"), {});
    TestTrue(TEXT("ds.Ship.Spares lists each spare"), Listed.Contains(TEXT("Drive.QuickLever")) && Listed.Contains(TEXT("Reactor.Stock")));
    Run(World, TEXT("ds.Ship.Spares"), { TEXT("give"), TEXT("Boosters.Stock") });
    TestEqual(TEXT("ds.Ship.Spares give adds one"), Ship->GetSpares().Num(), 3);
    const FString Refused = Run(World, TEXT("ds.Ship.Spares"), { TEXT("give"), TEXT("Reactor.Nonesuch") });
    TestTrue(TEXT("giving an unknown part adds nothing"), Ship->GetSpares().Num() == 3 && Refused.Contains(TEXT("give <part>")));
    Run(World, TEXT("ds.Ship.Spares"), { TEXT("clear") });
    TestEqual(TEXT("ds.Ship.Spares clear empties them"), Ship->GetSpares().Num(), 0);

    const FString Described = Run(World, TEXT("ds.Ship.Describe"), {});
    TestTrue(TEXT("ds.Ship.Describe names what is in each bay"), Described.Contains(TEXT("Reactor.TwinCore")) && Described.Contains(TEXT("Sensors.Stock")));
    TestTrue(TEXT("and says an empty slot reads stock"), Described.Contains(TEXT("Aux1")) && Described.Contains(TEXT("empty")));
    return true;
}
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.Commands
```

Expected: `FAILED`: `ds.Ship.Install Reactor.TwinCore: 1800 W` and the rest fail, because no `ds.Ship.*` command is registered and every `Run` prints `no command`.

- [ ] **Step 3: AddSpare and ClearSpares**

In `Source/DeepSpace/Ship/ShipSubsystem.h`'s public parts block, after `GetSpares`, add:

```cpp
    /** A new spare of the part with this id, aboard (ds.Ship.Spares give).
     *  False for an id that names no part. */
    bool AddSpare(FName PartId);

    /** No spares aboard (ds.Ship.Spares clear). */
    void ClearSpares();
```

In `ShipSubsystem.cpp`, after `FitPartById`, add:

```cpp
bool UShipSubsystem::AddSpare(FName PartId)
{
    UShipModuleDataAsset* Part = PartFor(PartId);
    if (!Part || !Register(Part))
    {
        return false;
    }
    FShipPartState Spare;
    Spare.PartId = PartId;
    Loadout.Spares.Add(Spare);
    return true;
}

void UShipSubsystem::ClearSpares()
{
    Loadout.Spares.Reset();
}
```

- [ ] **Step 4: The commands**

In `ShipSubsystem.cpp`'s anonymous namespace, after `NavChargeCommand`'s definition, add:

```cpp
    // -- parts, from the console, until landing and a sourcing spec exist ------
    // (wear and upgrades ruling 8). Developer's lines, not screens in the ship:
    // ds.Ship.Describe may print what no player sees.

    /** A catalogue part by id or display name, case-blind; the args are split
     *  on spaces, so "Twin-core reactor" arrives as two. */
    UShipModuleDataAsset* PartNamed(const UShipSubsystem& Ship, const TArray<FString>& Args)
    {
        const FString Wanted = FString::Join(Args, TEXT(" "));
        for (UShipModuleDataAsset* Part : Ship.GetCatalogue())
        {
            if (Wanted.Equals(Part->ModuleId.ToString(), ESearchCase::IgnoreCase)
                || Wanted.Equals(Part->DisplayName.ToString(), ESearchCase::IgnoreCase))
            {
                return Part;
            }
        }
        return nullptr;
    }

    FString CatalogueIds(const UShipSubsystem& Ship)
    {
        TArray<FString> Ids;
        for (const UShipModuleDataAsset* Part : Ship.GetCatalogue())
        {
            Ids.Add(Part->ModuleId.ToString());
        }
        return FString::Join(Ids, TEXT(", "));
    }

    void ShipInstall(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Ship.Install"));
        if (!Ship)
        {
            return;
        }
        const UShipModuleDataAsset* Part = Args.IsEmpty() ? nullptr : PartNamed(*Ship, Args);
        if (!Part)
        {
            Out.Logf(TEXT("ds.Ship.Install <part>: an id or a name, one of %s"), *CatalogueIds(*Ship));
            return;
        }
        if (Ship->FitPartById(Part->ModuleId))
        {
            Out.Logf(TEXT("%s is fitted (%s)."), *Part->DisplayName.ToString(), *Part->ModuleId.ToString());
        }
        else
        {
            Out.Logf(TEXT("%s cannot be fitted."), *Part->ModuleId.ToString());
        }
    }

    void ShipSpares(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Ship.Spares"));
        if (!Ship)
        {
            return;
        }
        if (!Args.IsEmpty() && Args[0].Equals(TEXT("clear"), ESearchCase::IgnoreCase))
        {
            Ship->ClearSpares();
            Out.Log(TEXT("No spares aboard."));
            return;
        }
        if (!Args.IsEmpty() && Args[0].Equals(TEXT("give"), ESearchCase::IgnoreCase))
        {
            TArray<FString> Rest = Args;
            Rest.RemoveAt(0);
            const UShipModuleDataAsset* Part = Rest.IsEmpty() ? nullptr : PartNamed(*Ship, Rest);
            if (!Part || !Ship->AddSpare(Part->ModuleId))
            {
                Out.Logf(TEXT("ds.Ship.Spares give <part>: an id or a name, one of %s"), *CatalogueIds(*Ship));
                return;
            }
            Out.Logf(TEXT("A spare %s is aboard."), *Part->DisplayName.ToString());
            return;
        }
        const TArray<FShipPartState>& Spares = Ship->GetSpares();
        Out.Logf(TEXT("%d spares aboard. ds.Ship.Install <part> fits one; ds.Ship.Spares give <part> | clear."), Spares.Num());
        for (int32 Index = 0; Index < Spares.Num(); ++Index)
        {
            Out.Logf(TEXT("%2d  %s"), Index, *Spares[Index].PartId.ToString());
        }
    }

    void ShipDescribe(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        const UShipSubsystem* Ship = ShipIn(World, Out, TEXT("ds.Ship.Describe"));
        if (!Ship)
        {
            return;
        }
        for (const EShipBay Bay : ShipBay::All())
        {
            FString Line = FString::Printf(TEXT("%-12s "), *ShipBay::Name(Bay).ToString());
            if (const UShipModuleDataAsset* Part = Ship->GetFittedPart(Bay))
            {
                Line += FString::Printf(TEXT("%s  draw %.0f W"), *Part->ModuleId.ToString(), Part->PowerDraw);
                for (const TPair<EShipRating, double>& Rated : Part->Ratings)
                {
                    Line += FString::Printf(TEXT("  %s %s"), *ShipParts::RatingName(Rated.Key).ToString(), *FString::SanitizeFloat(Rated.Value));
                }
            }
            else
            {
                Line += TEXT("empty: reads the stock part, draws nothing");
            }
            Out.Log(Line);
        }
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice ShipInstallCommand(
        TEXT("ds.Ship.Install"), TEXT("'ds.Ship.Install <part>': fit a part by id or name; a spare with that id first, else a new one. The displaced part becomes a spare."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShipInstall));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice ShipSparesCommand(
        TEXT("ds.Ship.Spares"), TEXT("'ds.Ship.Spares': the spares aboard. 'give <part>' adds one; 'clear' empties them."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShipSpares));
    FAutoConsoleCommandWithWorldArgsAndOutputDevice ShipDescribeCommand(
        TEXT("ds.Ship.Describe"), TEXT("Every bay: its part, draw and ratings. A developer's line, not a screen in the ship."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ShipDescribe));
```

- [ ] **Step 5: Run it and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.Commands
```

Expected: `passed: 1`.

- [ ] **Step 6: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipLoadoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(ship): ds.Ship.Install, ds.Ship.Spares and ds.Ship.Describe

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 7: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '        if (Ship->FitPartById(Part->ModuleId))' '        if (Ship->AddSpare(Part->ModuleId))' DeepSpace.Ship.Parts.Commands && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp '|| Wanted.Equals(Part->DisplayName.ToString(), ESearchCase::IgnoreCase))' ')' DeepSpace.Ship.Parts.Commands && \
./build.sh
```

Expected: `KILLED` twice, then `Result: Succeeded`:
1. install only adds a spare;
2. a display name no longer finds the twin core.

---

## Task 12 (S7): The plain state round-trips, and restores by bay name

**Owner:** S. **Depends on:** Task 11. Spec decision 11. The fields exist for slices 3 and 4. The restore is what slice 3's save calls.

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public parts block: `RestoreLoadout`)
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.cpp` (`RestoreLoadout`, after `ClearSpares`)
- Modify: `Source/DeepSpace/Tests/ShipLoadoutTest.cpp` (append `DeepSpace.Ship.Parts.StateRoundTrips`)

**Interfaces:**
- Consumes: `FShipLoadoutState` (D1); `PartFor`, `Register`, `SetBayPart` (S2, S5).
- Produces: `int32 UShipSubsystem::RestoreLoadout(const FShipLoadoutState& State)`, which returns how many entries fell back.

- [ ] **Step 1: Write the failing test**

In `Source/DeepSpace/Tests/ShipLoadoutTest.cpp`, add these includes: `#include "Algo/Reverse.h"`, `#include "Serialization/MemoryReader.h"`, `#include "Serialization/MemoryWriter.h"`, `#include "Serialization/ObjectAndNameAsStringProxyArchive.h"`. Then append, before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShipPartsStateRoundTripsTest, "DeepSpace.Ship.Parts.StateRoundTrips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Decision 11: the loadout is plain and serialisable from slice 1, so the
 * save (slice 3) is a slice and not a rewrite. It goes through Unreal's own
 * struct serialiser and back equal, wear fields included. It restores by bay
 * name, never position, so a state in any order is the same loadout. And
 * what a state cannot name -- an unknown part, a part in the wrong bay, a
 * bay the ship has not got, a bay it lacks -- falls back to stock, counted.
 */
bool FShipPartsStateRoundTripsTest::RunTest(const FString& Parameters)
{
    using namespace SkyTestWorld;
    using namespace ShipLoadoutTestLocal;
    UScriptStruct* Struct = FShipLoadoutState::StaticStruct();

    FShipLoadoutState Written;
    {
        FSkyWorld First(TEXT("RoundTripFirstWorld"));
        TestEqual(TEXT("the stock ship fits"), StockShip::Install(First.Ship), 6);
        TestTrue(TEXT("the twin core fits"), First.Ship->FitPartById(TEXT("Reactor.TwinCore")));
        TestTrue(TEXT("and a spare quick lever is aboard"), First.Ship->AddSpare(TEXT("Drive.QuickLever")));
        Written = First.Ship->GetLoadoutState();
    }
    // The wear fields are slice 4's, but the struct carries them now: give
    // them values, so the round trip proves they travel.
    if (!TestEqual(TEXT("two spares: the stock reactor and the quick lever"), Written.Spares.Num(), 2))
    {
        return false;
    }
    Written.Spares[0].AgeJumps = 12.5;
    Written.Spares[0].LifeJumps = 151.25;
    Written.Spares[0].bHasLife = true;
    Written.Spares[0].Symptom = TEXT("Reactor.Stutter");
    Written.Spares[0].Repairs = 2;
    Written.Spares[0].bOriginal = true;
    ShipParts::FindBay(Written, EShipBay::Reactor)->LivesDrawn = 3;

    // -- through the struct serialiser and back ----------------------------------
    TArray<uint8> Bytes;
    {
        FMemoryWriter Writer(Bytes);
        FObjectAndNameAsStringProxyArchive Archive(Writer, false);
        Struct->SerializeItem(Archive, &Written, nullptr);
    }
    FShipLoadoutState Read;
    {
        FMemoryReader Reader(Bytes);
        FObjectAndNameAsStringProxyArchive Archive(Reader, false);
        Struct->SerializeItem(Archive, &Read, nullptr);
    }
    TestTrue(TEXT("written and read back, every field is equal"), Struct->CompareScriptStruct(&Written, &Read, PPF_None));

    // -- restored by name, in any order -----------------------------------------------
    {
        FShipLoadoutState Shuffled = Read;
        Algo::Reverse(Shuffled.Bays);
        FSkyWorld Second(TEXT("RoundTripSecondWorld"));
        TestEqual(TEXT("a state with its bays in any order restores with nothing falling back"), Second.Ship->RestoreLoadout(Shuffled), 0);
        TestTrue(TEXT("and is the loadout that was written"), Struct->CompareScriptStruct(&Second.Ship->GetLoadoutState(), &Read, PPF_None));
        TestEqual(TEXT("the twin core runs the ship"), Second.Ship->GetReactorOutput(), 1800.0f);
        TestEqual(TEXT("and today's 620 W is drawn"), Draws(*Second.Ship), 620.0f, 1e-2f);
    }

    // -- what a state cannot name falls back to stock, by name -----------------------
    {
        FShipLoadoutState Odd = Read;
        ShipParts::FindBay(Odd, EShipBay::Reactor)->Part.PartId = TEXT("Reactor.Nonesuch");
        ShipParts::FindBay(Odd, EShipBay::Drive)->Part.PartId = TEXT("Reactor.TwinCore");
        Odd.Bays.RemoveAll([](const FShipBayState& Entry) { return Entry.Bay == ShipBay::Name(EShipBay::Lights); });
        FShipBayState Galley;
        Galley.Bay = TEXT("Galley");
        Odd.Bays.Add(Galley);
        FShipPartState Unknown;
        Unknown.PartId = TEXT("Aux.Nonesuch");
        Odd.Spares.Add(Unknown);

        FSkyWorld Third(TEXT("RoundTripThirdWorld"));
        TestEqual(TEXT("five entries fall back: an unknown part, a part in the wrong bay, a missing bay, an unknown bay, an unknown spare"),
                  Third.Ship->RestoreLoadout(Odd), 5);
        const auto Holds = [&](EShipBay Bay, const TCHAR* Id)
        {
            const UShipModuleDataAsset* Part = Third.Ship->GetFittedPart(Bay);
            return Part && Part->ModuleId == FName(Id);
        };
        TestTrue(TEXT("the unknown reactor becomes the stock reactor"), Holds(EShipBay::Reactor, TEXT("Reactor.Stock")));
        TestTrue(TEXT("the reactor in the drive bay becomes the stock drive"), Holds(EShipBay::Drive, TEXT("Drive.Stock")));
        TestTrue(TEXT("the missing lights bay gets the stock lights"), Holds(EShipBay::Lights, TEXT("Lights.Stock")));
        TestEqual(TEXT("the spares keep what they can name, with their state"), SpareIds(*Third.Ship), FString(TEXT("Reactor.Stock,Drive.QuickLever")));
        TestEqual(TEXT("and the wear fields came with them"), Third.Ship->GetSpares()[0].AgeJumps, 12.5);
        TestEqual(TEXT("the ship draws today's 620 W"), Draws(*Third.Ship), 620.0f, 1e-2f);
    }
    return true;
}
```

- [ ] **Step 2: Run it and see it fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh
```

Expected: the build fails, `no member named 'RestoreLoadout' in 'UShipSubsystem'`.

- [ ] **Step 3: RestoreLoadout**

In `ShipSubsystem.h`'s public parts block, after `ClearSpares`, add:

```cpp
    /**
     * Sets the whole loadout from State (decision 11), as slice 3's save
     * will. Bays are found by name, never position. Whatever State cannot
     * name falls back and is counted:
     * - an unknown part, or a part in the wrong bay: the bay's stock part;
     * - a core bay State lacks: its stock part;
     * - a bay this ship has not got: ignored;
     * - a spare with an unknown id: dropped.
     * Returns how many entries fell back, each also logged by name.
     */
    int32 RestoreLoadout(const FShipLoadoutState& State);
```

In `ShipSubsystem.cpp`, after `ClearSpares`, add:

```cpp
int32 UShipSubsystem::RestoreLoadout(const FShipLoadoutState& State)
{
    int32 Fallbacks = 0;
    for (const FShipBayState& Entry : State.Bays)
    {
        if (!ShipBay::FromName(Entry.Bay))
        {
            UE_LOG(LogTemp, Warning, TEXT("Ship: a loadout names a bay this ship has not got, %s; ignored."), *Entry.Bay.ToString());
            ++Fallbacks;
        }
    }

    Loadout.Spares.Reset();
    for (int32 Index = 0; Index < ShipBay::All().Num(); ++Index)
    {
        const EShipBay Bay = ShipBay::All()[Index];
        // By name, never by position: a bay added to EShipBay later shifts
        // nothing saved (decision 11).
        const FShipBayState* Entry = State.Bays.FindByPredicate([Bay](const FShipBayState& Candidate) { return Candidate.Bay == ShipBay::Name(Bay); });
        FShipPartState Part = Entry ? Entry->Part : FShipPartState();
        bool bFellBack = !Entry && ShipBay::IsCore(Bay);
        if (!Part.PartId.IsNone())
        {
            UShipModuleDataAsset* Asset = PartFor(Part.PartId);
            const bool bFits = Asset && Register(Asset)
                && (ShipBay::IsAux(Bay) ? ShipBay::IsAux(Asset->Bay) : Asset->Bay == Bay);
            if (!bFits)
            {
                UE_LOG(LogTemp, Warning, TEXT("Ship: %s cannot be in the %s bay; its stock part is fitted instead."),
                       *Part.PartId.ToString(), *ShipBay::Name(Bay).ToString());
                bFellBack = true;
            }
        }
        if (bFellBack)
        {
            ++Fallbacks;
            Part = FShipPartState();
            UShipModuleDataAsset* Stock = ShipBay::IsCore(Bay) ? PartFor(ShipBay::StockPartId(Bay)) : nullptr;
            if (Stock && Register(Stock))
            {
                Part.PartId = Stock->ModuleId;
            }
        }
        SetBayPart(Bay, Part);
        ShipParts::FindBay(Loadout, Bay)->LivesDrawn = Entry ? Entry->LivesDrawn : 0;
    }

    for (const FShipPartState& Spare : State.Spares)
    {
        UShipModuleDataAsset* Asset = PartFor(Spare.PartId);
        if (Asset && Register(Asset))
        {
            Loadout.Spares.Add(Spare);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("Ship: a spare names no part, %s; dropped."), *Spare.PartId.ToString());
            ++Fallbacks;
        }
    }
    return Fallbacks;
}
```

- [ ] **Step 4: Run it and see it pass**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh DeepSpace.Ship.Parts.StateRoundTrips
```

Expected: `passed: 1`.

- [ ] **Step 5: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipLoadoutTest.cpp && \
git commit -m "$(cat <<'EOF'
feat(ship): RestoreLoadout -- the plain state round-trips and restores by bay name

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 6: Prove it can fail**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'const FShipBayState* Entry = State.Bays.FindByPredicate([Bay](const FShipBayState& Candidate) { return Candidate.Bay == ShipBay::Name(Bay); });' 'const FShipBayState* Entry = State.Bays.IsValidIndex(Index) ? &State.Bays[Index] : nullptr;' DeepSpace.Ship.Parts.StateRoundTrips && \
Tools/mutate.sh Source/DeepSpace/Ship/ShipSubsystem.cpp 'ShipParts::FindBay(Loadout, Bay)->LivesDrawn = Entry ? Entry->LivesDrawn : 0;' 'ShipParts::FindBay(Loadout, Bay)->LivesDrawn = 0;' DeepSpace.Ship.Parts.StateRoundTrips && \
./build.sh
```

Expected: `KILLED` twice, then `Result: Succeeded`:
1. a reversed state restores by position, so every bay is wrong;
2. the reactor bay's lives are lost.

---

## Task 13 (S8): Re-plan the landing steps this slice invalidates (sign-off 29)

**Owner:** S. **Depends on:** Task 12, and **the gate**: `feat/landing-a-procgen` merged to `main`, and **R's verdict settled** -- not R merged. R's spike stopped on FLOAT FLOOR (`8d66dbd`), which the landing plan sends to the developer; the ruling may re-plan R and slice (b)'s shader tasks (T1, T2, T7) in the landing plan on `main`, and R may never merge in its present form. So the gate is: the developer has ruled, any re-plan of the landing plan that ruling causes is committed on `main`, and no landing orchestrator holds an uncommitted edit to the landing plan. Waiting on R's merge instead could stall this slice indefinitely, and landing (b) waits on this slice. Spec *Seams with work in flight*; sign-off 29.

This task runs immediately before Task 14, and its commit is the last one on `feat/wear-1-s` before Task 14's: the landing plan is edited on `wear-1-s` for as short a time as possible, and Task 14 Step 1 checks it has not moved on `main` since.

**Files:**
- Modify: `docs/superpowers/plans/2026-09-27-landing-slice-1.md`. Line numbers are from `e5d257c`; search for the quoted text:
  - *Execution order* (line 93);
  - Task 13 (B0), heading line 4839: insert a new Step 0a after Step 0 (about line 4847-4871) and before Step 1 (about line 4873);
  - a new amendment section before `# Slice (b)` (line 4835);
  - Task 24 (S2): its *Files* (lines 9625-9626), Step 4 (lines 9894-9918);
  - Task 25 (S3): its *Files* (lines 10009-10010).

No test and no mutation: this task writes a plan. Its checks are Step 2's grep (what slice 1 renamed) and Step 5's (the files slice 1 edits), with Step 9 checking nothing stale is left.

**Interfaces:**
- Consumes: slice 1 as built. `FShipRatings UShipSubsystem::GetRatings() const`. `ApplyAllocation`'s boosters block exactly as Task 6 (S1) wrote it: from the comment `// The boosters' want has one writer, here (wear sign-off 29): the fitted` through `const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * BoosterFeed;`.
- Produces: a landing plan whose slice (b) and (c) steps apply to the post-slice-1 tree. There is **one writer of the boosters' want**: `ApplyAllocation`, which writes the rated want plus `HoldWant`.

- [ ] **Step 1: The gate, then bring main in**

First, the orchestrator confirms R's verdict is settled: either `feat/landing-a-relief` is merged to `main`, or the developer has ruled on R's FLOAT FLOOR report and the landing plan's re-plan for that ruling is committed on `main` (ask the landing orchestrator which commit it is, and read it in `git log main -- docs/superpowers/plans/2026-09-27-landing-slice-1.md`). A ruling recorded only in the spec (such as `146ddbe`) is not enough: the plan's shader tasks may still be re-planned. If neither holds, wait. Then:

```bash
cd /home/matt/Development/deepspace && \
git log --oneline -1 --fixed-strings --grep='merge: landing-a-procgen' main | grep -q . && echo "P merged" && \
[ -z "$(git status --short -- docs/superpowers/plans/2026-09-27-landing-slice-1.md)" ] && \
{ [ ! -d .worktrees/landing-a-relief ] || \
  [ -z "$(git -C .worktrees/landing-a-relief status --short -- docs/superpowers/plans/2026-09-27-landing-slice-1.md)" ]; } && \
echo "nobody is editing the landing plan" && \
git -C .worktrees/wear-1-s merge -q --no-edit main && git -C .worktrees/wear-1-s log --oneline -1
```

Expected: `P merged`, `nobody is editing the landing plan`, then a clean merge. Task 14 Step 1 compares the landing plan on `main` against the `main` merged here. If a check exits 1, wait: the landing plan is still landing's to amend. Do Steps 2-10 now, without a break, so the edit is made on the text just merged. After the merge, run `./build.sh` in `wear-1-s`.

- [ ] **Step 2: Find every landing step that names what slice 1 changed**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && grep -nE \
'BoostersWant|LightsWant|DefaultReactorOutput|InstallModule|RemoveModule|GetInstalledModules|UShipSubsystem::GetWindingWant|UShipSubsystem::GetChartRangeLy|CVarChargeSeconds|CVarWindingWant|CVarRangeLy|CVarDriveResponse|ds\.Nav\.RangeLy|ds\.Nav\.ChargeSeconds|ds\.Nav\.WindingWant|ds\.Drive\.Response|BoosterFeed|GetReadout|DRAWN|SPARE|Cruise\(\)\.LinearAcceleration|SetReactorOutput' \
docs/superpowers/plans/2026-09-27-landing-slice-1.md
```

Expected: these hits, and no others (line numbers approximate):
- about 9678: `Power.SetReactorOutput(1400.0f);`;
- about 9895: `replace the three lines from \`const float BoosterFeed = ...\``;
- about 9908: `PowerState.SetWant(ShipPower::Boosters, BoostersWant + HoldWant);`;
- about 9912: `SplitBoosters(..., HoldWant, BoostersWant)`;
- about 11337: `FShipFlightLimits::Cruise().LinearAcceleration`;
- about 19992 and 19996: `ds.Nav.ChargeSeconds 5`.

If landing (a), or the re-plan R's FLOAT FLOOR ruling caused, added further hits, re-plan each by Step 8's rule too, and record it in the amendment's list.

- [ ] **Step 3: Task 24 (S2)'s Files name what it now edits**

In the landing plan, replace:

```text
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public getters; private `HoldWant`,
  `LastSplit`), `ShipSubsystem.cpp` (`ApplyAllocation`, the `CVarHoldWatts` default)
```

with:

```text
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h` (public getters; private `HoldWant`,
  `LastSplit`), `ShipSubsystem.cpp` (`ApplyAllocation`'s boosters block, as wear slice 1 left
  it; the `CVarHoldWatts` default)
```

- [ ] **Step 4: Task 24 (S2) Step 4 writes the boosters' want in one place**

In the landing plan, replace the text from `` `ShipSubsystem.cpp`: set `CVarHoldWatts`' default to `ShipPower::DefaultHoldWattsPerG`. In `` through `` (`ShipSubsystem.cpp:520`). Just before `FlightState.SetLimits(Limits);`: ``. That span is the lead-in sentence, the code block, and the `EngineFeed` sentence. Replace it with:

````markdown
`ShipSubsystem.cpp`: set `CVarHoldWatts`' default to `ShipPower::DefaultHoldWattsPerG`. In
`ApplyAllocation`, replace everything from the comment `// The boosters' want has one writer,
here (wear sign-off 29): the fitted` down to and including
`const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * BoosterFeed;` with
the block below. (Amended for wear slice 1: `BoostersWant` is no longer a constant but the
fitted boosters part's rating, `Ratings.BoostersWant`, where `Ratings` is `ApplyAllocation`'s
first local. The boosters' want has one writer, this block, as the rating plus the hold. So a
fit, which never writes the want, and the hold cannot write it from two places in one frame.)

```cpp
    // The hold (landing decision 5): only under a solid world's drive floor,
    // airborne -- landed is slice (c)'s, always airborne here. Recomputed only
    // when it moves by more than a watt, so the split is not re-solved every
    // frame for nothing.
    const float WantNow = ShipPower::HoldWant(FlightState.GetLocalGravity().Size(), FlightState.GetDepthUnderDriveFloor(),
                                              FMath::Max(0.0f, CVarHoldWatts.GetValueOnGameThread()), true);
    if (FMath::Abs(WantNow - HoldWant) > 1.0f || (WantNow == 0.0f && HoldWant != 0.0f))
    {
        HoldWant = WantNow;
    }

    // The boosters' want has one writer, here (wear sign-off 29): the fitted
    // part's rating plus the hold. A fit changes the rating and this pass
    // writes the sum, so a fit and the hold never write it from two places.
    const float ManoeuvreWant = static_cast<float>(Ratings.BoostersWant);
    const float BoostersWantNow = ManoeuvreWant + HoldWant;
    if (PowerState.GetWant(ShipPower::Boosters) != BoostersWantNow)
    {
        PowerState.SetWant(ShipPower::Boosters, BoostersWantNow);
    }

    // Asked for fresh every frame and never stored beyond it.
    LastSplit = ShipPower::SplitBoosters(PowerState.GetShare(ShipPower::Boosters), HoldWant, ManoeuvreWant);
    const float EngineFeed = PowerState.GetSatisfaction(ShipPower::Engine);
    const float Thrust = StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * LastSplit.ManoeuvreFeed;
```

The block keeps `const float EngineFeed = ...`: `ChargeJumpDrive` reads it further down in
`ApplyAllocation`. Just before `FlightState.SetLimits(Limits);`:
````

The `const float WantNow = ...` statement is kept word for word, with its comment's "-- landed is slice (c)'s, always airborne here". Task 43 (C4) Step (c) replaces exactly that statement and deletes exactly that phrase, so it applies unchanged.

- [ ] **Step 5: Find every landing step that edits or calls a file slice 1 edits**

Step 2 finds renamed symbols only. A landing step can also edit a test file this slice rewrote, without naming anything that changed. So grep for the files:

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && grep -nE \
'ShipSkyTest|JumpWindsTest|StockShip|HumComponent|NavigationWidget|EngineeringConsole|ShipLoadoutTest|LampPanelsDim|NavScreenTest|ChartLayoutTest|SliceChooseTest|ShipJumpTest|ShipScreensAgree|ShipConsole|DeepSpaceGameMode|ShipModuleDataAsset|ShipParts' \
docs/superpowers/plans/2026-09-27-landing-slice-1.md
```

Expected: these hits, and no others (line numbers approximate), each of which applies unchanged:
- about 9544: `ShipJumpTest.cpp:385` reads `GetRoom` between stars. Slice 1 edits only `ShipJumpTest.cpp:234`, a read of the chart range. Unchanged.
- `Tests/StockShip.h` and `StockShip::Install(...)`, about 9649, 9726, 10035, 10060, 10539, 10617, 11501, 11556, 11592, 11658, 11697, 11733, 19464, 19478, 19557, 19644, 19684 and 19723: slice 1 keeps `StockShip::Install(UShipSubsystem*)`'s signature, and it fits six parts with the same 620 W off the top and the same wants (ruling 1). Unchanged.
- `ShipHumComponent` / `DeepSpace.Ship.HumComponent`, about 10009, 10030, 10063, 10072, 10136, 10162, 10173 and 10179: Task 25 (S3). Its *Files* line is amended in Step 6; `UShipHumComponent::AskShip(const UShipSubsystem&)` keeps its signature, and the test it runs, `HumComponent`, is one slice 1 re-ran green on the stock ship. Unchanged apart from Step 6's note.
- `ShipSkyTest.cpp`, about 12352, 12461 and 12731: Task 32 (T2) replaces the relief and crater knob lines (`FScopedCVar FaceRelief(TEXT("ds.Sky.Relief"), 0.321f);` and the next two, about lines 390-392) and the two assertions that read `SkyMaterial::Relief` and `SkyMaterial::Cratering` (about 433-435). Slice 1 rewrote only the hog block (from `UShipModuleDataAsset* Hog = NewObject<UShipModuleDataAsset>();`, about 667-687), which T2 does not quote. Unchanged. (If R's ruling re-planned T2, check the re-planned text quotes nothing in the hog block.)
- `JumpWindsTest.cpp`, about 18914, 19242 and 19339: Task 43 (C4) inserts a landed block directly before `Fresh->EndPlay(EEndPlayReason::Quit);`, on `Default->`. Slice 1 keeps `UShipSubsystem* Default` and `Fresh->EndPlay(...)`, and changes only `Default->InstallModule(Module);` to `Default->FitPart(Module);` above them. The block's literal `450.0f` (`> 450.0f` hovering, `landed, the boosters want exactly 450 W`) is the stock boosters part's rating, which ruling 1 keeps at 450 W and `DeepSpace.Ship.Parts.Contract` pins. Unchanged.

Task 24 (S2) Step 1's `ParkedIsWhole` asserts the same literal (`450.0f + Ship->GetHoldWant()`), on the same stock ship, and is left as it is for the same reason: a test on the stock ship states the stock figures, as every pre-existing test here does (*Global Constraints*). The two literals stay consistent; neither is rewritten to `GetRatings().BoostersWant`.

No hit for `NavigationWidget`, `EngineeringConsole`, `ShipLoadoutTest`, `LampPanelsDim`, `NavScreenTest`, `ChartLayoutTest`, `SliceChooseTest`, `ShipScreensAgree`, `ShipConsole`, `DeepSpaceGameMode`, `ShipModuleDataAsset` or `ShipParts`. Any hit not listed (for example, one added by R's re-plan) is re-planned by Step 8's rule and recorded in the amendment's table.

- [ ] **Step 6: Task 25 (S3) notes what slice 1 already did to AskShip**

In the landing plan, immediately after the Task 25 *Files* line that reads `- Modify: \`Source/DeepSpace/Ship/ShipHumComponent.cpp\` (the CVar block, lines 16-26; \`AskShip\`,` and its continuation `  lines 87-118)`, add:

```text
  (Amended for wear slice 1: `AskShip` already reads `Ship.GetWindingWant()`, an instance call
  now, and its `Rated` is `Ship.GetRatings().LinearAcceleration`, the boosters part's rating.
  This task's replacement of the `Inputs.Push = ...` statement applies unchanged. Search for the
  code, not the line numbers.)
```

- [ ] **Step 7: Landing (b) opens only after slice 1 is on main**

In the landing plan's *Execution order*, replace `Task 13 (B0) the feat/landing-b branch and the three trees          (slice (a) merged)` with `Task 13 (B0) the feat/landing-b branch and the three trees          (slice (a) and wear slice 1 merged)`.

In Task 13 (B0), insert before `- [ ] **Step 1: Create the slice branch and the three track trees**`:

````markdown
- [ ] **Step 0a: Wear slice 1 is on main (wear sign-off 29)**

```bash
cd /home/matt/Development/deepspace && \
git log --oneline -1 --fixed-strings --grep='merge: wear and upgrades slice 1 -- the upgrade seam' main | grep -q . && echo "wear slice 1 merged"
```

Expected: `wear slice 1 merged`. If not, wait. Slice (b)'s track S edits `ShipSubsystem.*`,
`ShipPowerState.*` and `ShipHum*` against the tree slice 1 leaves, and Tasks 24 and 25 are written
against it (see *Amendment, 2026-09-27: wear and upgrades slice 1 lands before slice (b)*).

````

- [ ] **Step 8: Record the amendment**

In the landing plan, insert before the line `# Slice (b): terrain, gravity and the vertical lever -- fly down and hover over real ground`:

```markdown
## Amendment, 2026-09-27: wear and upgrades slice 1 lands before slice (b)

The wear and upgrades spec's sign-off 29 orders its slice 1, the upgrade seam, before slice (b).
It is built and merged, planned in `docs/superpowers/plans/2026-09-27-wear-slice-1-upgrade-seam.md`.
Slice 1 changed text that slice (b)'s steps quote:
- `BoostersWant`, `LightsWant` and `DefaultReactorOutput` are no longer `UShipSubsystem`
  constants. They are the fitted parts' ratings (`FShipRatings`, `GetRatings()`), with the stock
  numbers unchanged: 450 W, 300 W, 1400 W.
- `ApplyAllocation` begins with `const FShipRatings Ratings = GetRatings();`. It is the boosters'
  want's one writer, in a block that precedes the `BoosterFeed` lines.
- `GetWindingWant()` and `GetChartRangeLy()` are instance calls, and `GetChargeSeconds()` and
  `GetDriveResponse()` join them. The four CVars (`ds.Nav.RangeLy`, `.ChargeSeconds`,
  `.WindingWant`, `ds.Drive.Response`) default to -1, the fitted part's; 0 or more still
  overrides.
- `InstallModule`/`RemoveModule` are gone. `FitPart` fits a part into its bay, and `AddLoad`/
  `RemoveLoad` book a test's standing draw. `StockShip::Install` keeps its signature and fits
  six parts, with the same 620 W off the top.

**The rule for the boosters' want:** one writer, `ApplyAllocation`, sets it to the rated
boosters want plus `HoldWant`. `FitPart` never writes it.

Every step that names what changed, or edits a file slice 1 edited, was found by grep (by symbol, and by file) and re-planned or checked against the post-slice-1 tree:

| Step | Was | Now |
|---|---|---|
| Task 24 (S2), *Files* | `ApplyAllocation` | the boosters block as slice 1 left it |
| Task 24 (S2), Step 4 | replaced the three `BoosterFeed` lines; wrote the boosters' want from the old header constant plus the hold, inside the hold's change test; split the share against that constant | replaces slice 1's one-writer block and those lines; the want is written once, from `Ratings.BoostersWant + HoldWant`; the split is against `ManoeuvreWant`, the rating. The `WantNow` statement is unchanged, so Task 43 (C4) Step (c) applies as written. |
| Task 24 (S2), Step 1, `ParkedIsWhole` | -- | unchanged: its literal 450 W plus the hold is the stock boosters' rating on `StockShip::Install` (ruling 1), as every stock-ship test states it |
| Task 32 (T2), `ShipSkyTest.cpp` | -- | unchanged: it replaces the relief and crater knob lines and their two assertions; slice 1 rewrote only the hog block further down, which T2 does not quote |
| Task 43 (C4), `JumpWindsTest.cpp` | -- | unchanged: its landed block goes before `Fresh->EndPlay(EEndPlayReason::Quit);` on `Default->`, both of which slice 1 kept (it changed only `Default->InstallModule` to `Default->FitPart` above). Its literal 450 W is the stock boosters' rating, as in `ParkedIsWhole` |
| Every step that calls `StockShip::Install` | -- | unchanged: same signature, six parts, the same 620 W off the top and the same wants |
| Task 25 (S3), *Files* | `AskShip` as on `202703c` | noted: slice 1 made the engine feed an instance call and `Rated` the boosters' rating; the step applies unchanged |
| Task 13 (B0) | slice (a) merged | and wear slice 1 merged (new Step 0a) |
| Task 24 (S2), Step 1, `Stock()`'s `Power.SetReactorOutput(1400.0f)` | -- | unchanged: a pure power state built by hand, with numbers, not names |
| Task 30 (S8), `FShipFlightLimits::Cruise().LinearAcceleration` | -- | unchanged: the playtest runs on `StockShip::Install`, whose boosters rate exactly `Cruise()`'s 2 km/s^2 |
| Task 47 (C8), Steps 5 and 7, `ds.Nav.ChargeSeconds 5` | -- | unchanged: 5 is 0 or more, so it still overrides the drive part |

Line numbers in slice (b) and (c) steps that point into `ShipSubsystem.*`, `ShipHumComponent.cpp`
or the four CVars' block have moved. As everywhere in this plan, search for the quoted code.
The CLAUDE.md anchors slice (b) and (c) edit are ones slice 1 left alone:
- the power paragraph's "no cutoff, no alarm" sentence (Task 24 Step 6);
- the hum's sentence and row (Task 25);
- the tunables table's insertion point before `ds.HUD.TargetMinPixels`;
- *Architecture*'s `Sky/` line.
```

- [ ] **Step 9: Check nothing stale is left**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
grep -nE 'PowerState\.SetWant\(ShipPower::Boosters, BoostersWant \+|SplitBoosters\([^)]*, BoostersWant\)|InstallModule\(' \
docs/superpowers/plans/2026-09-27-landing-slice-1.md; echo "stale: $?"
```

Expected: `stale: 1`. grep found nothing: no step still writes the old constant, and none calls the retired `InstallModule`. (The literal 450 W in `ParkedIsWhole` and C4 is deliberate; Step 5.)

- [ ] **Step 10: Commit**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && git add docs/superpowers/plans/2026-09-27-landing-slice-1.md && \
git commit -m "$(cat <<'EOF'
docs: landing plan amended for wear slice 1 -- one writer of the boosters' want

Task 24 (S2) writes the boosters' want once, in ApplyAllocation, as the
fitted part's rating plus the hold; Task 25 (S3) notes AskShip's instance
call; Task 13 (B0) waits on wear slice 1. Every landing step naming what
slice 1 changed, or editing a file slice 1 edited (T2's ShipSkyTest, C4's
JumpWindsTest, every StockShip caller), is listed in the amendment,
re-planned or checked unchanged.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

---

## Task 14 (Z): Slice 1 done -- the documentation, the whole suite, the merge

**Owner:** S tree for the documentation and the checks; orchestrator for the merge. **Depends on:** Task 13, directly after it; its gate still holds; the landing plan has not moved on `main` since Task 13 Step 1; and no landing (a) task that edits `CLAUDE.md` is in progress (R's are R1 Step 18, R4 Step 7 and R5 Step 5, all to paragraphs this task does not touch -- *Execution order*). R need not be merged. Spec *Documentation changes* (slice 1's share), *The four slices: Slice 1*, *Done when*.

**Files:**
- Modify: `CLAUDE.md`:
  - *Architecture* (after the `Ship/ShipDressing*` bullet);
  - *Screens, the pointer, and power* (a new paragraph after the one ending `say so rather than tuning it.`);
  - a new section *Parts and bays* before `## Playtest console`;
  - *Playtest console* (a new block after the paragraph ending `(*The dressing*).`);
  - *Where each tunable lives* (four rows, and the *Tunables that are not CVars* paragraph).
- Modify: `Source/DeepSpace/Ship/ShipPowerState.h` (the class comment: the **Draws** bullet, line 31, and the "Nothing drifts" paragraph, lines 38-39)
- Modify: `docs/decisions/0003-ship-state-as-subsystem.md` (append an amendment)
- Modify: `docs/superpowers/specs/2026-09-25-navigation-and-arrival-design.md` (the *Jump range* fake, lines 762-763)
- Modify: `docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md` (the *Status* line, lines 4-5)

- [ ] **Step 1: Bring D and main in a last time**

```bash
cd /home/matt/Development/deepspace && \
{ [ ! -d .worktrees/landing-a-relief ] || [ -z "$(git -C .worktrees/landing-a-relief status --short -- CLAUDE.md)" ]; } && \
[ -z "$(git status --short -- CLAUDE.md)" ] && echo "nobody is editing CLAUDE.md" && \
git diff --stat "$(git merge-base main feat/wear-1-s)" main -- docs/superpowers/plans/2026-09-27-landing-slice-1.md && \
cd .worktrees/wear-1-s && git merge -q --no-edit feat/wear-1-d && git merge -q --no-edit main && \
git log --oneline -3
```

The merge base of `main` and `feat/wear-1-s` is the `main` Task 13 Step 1 merged, since nothing merged `main` in between. Expected: `nobody is editing CLAUDE.md`, an **empty** diff stat (the landing plan on `main` is the text Task 13 amended), then clean merges, or "Already up to date". If the `CLAUDE.md` check exits 1, an R task (or someone on `main`) is mid-edit: wait until it commits. If the diff stat is not empty, the landing plan moved on `main` after Task 13 read it: merge `main`, resolve any conflict in the landing plan by keeping `main`'s text and re-applying Task 13's edits to it, then re-run Task 13 Steps 2, 5 and 9 and amend Task 13's commit before going on. A conflict in `CLAUDE.md` should not happen (R's paragraphs are not this task's); if it does, stop and report it.

- [ ] **Step 2: CLAUDE.md, *Architecture***

After the bullet that begins `` - `Ship/ShipDressing*` `` and ends `(*The dressing*).`, add:

```markdown
- `Ship/ShipParts.*`, `Ship/ShipPartCatalogue.h` -- the pure parts core: the
  bays, the ratings and the stock ship as numbers, the catalogue's rules, and
  the plain loadout state. `UShipSubsystem` fits parts and derives the rated
  values from them (*Parts and bays*).
```

- [ ] **Step 3: CLAUDE.md, *Screens, the pointer, and power***

After the paragraph that ends `principle -- say so rather than tuning it.`, add a paragraph of its own. Leave that paragraph's own sentences as they are: landing's Task 24 Step 6 replaces one of them.

```markdown
The reactor's output and every want are the fitted parts' ratings
(*Parts and bays*), and draws are booked by bay. **The engineering console
shows one nameplate per fitted part and nothing else**: no total drawn, no
headroom, no percentage, no tier, no comparison. The lived-in spec's
decision 11 made the reactor's readout a nameplate, and a total beside it is
a utilisation meter. The laptop's per-consumer watts are each consumer's own
share of its own want, never a total.
```

- [ ] **Step 4: CLAUDE.md, a new section *Parts and bays***

Insert before `## Playtest console`:

```markdown
## Parts and bays

The ship is fitted, not fixed
(`docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md`).
Slice 1, the upgrade seam, is built. The install act, the save and wear are
not. **A part is a module in a bay, and it changes a number.** An upgrade is
a different number, never a different model.

- **Six fixed bays and two auxiliary slots** (`EShipBay`, `Ship/ShipParts.h`):
  reactor, drive, boosters, lights, life support, sensors, `Aux1` and `Aux2`.
  Each holds one part.
  - `UShipSubsystem::FitPart` swaps, and the displaced part joins the spares.
  - A part whose `Bay` is `None`, or with no id, is refused.
  - A core part is only ever swapped; `RemovePart` is for aux slots.
- **An empty bay reads the stock part and draws nothing**, so a bare test
  world is today's bare world. A played ship fits the six stock parts, from
  `BP_DeepSpaceGameMode`'s list, which is what `Tests/StockShip.h` installs.
  - **The stock ship is today's ship**: 1400 W, 620 W of draws, 1370 W at
    rest, 380 W winding in 45 s, 3 notches/s, 12 ly.
- **Rated values are derived, never stored.** `GetRatings()` is the stock
  numbers with every fitted part's ratings over them.
  - Each rating belongs to one bay, and no part rates a top speed.
  - A fit moves the supply and the lights' want at once.
  - **The boosters' want has one writer, `ApplyAllocation`.** Landing's
    hold adds to it there.
- **Draws are booked by bay** (`Bay.Lights`). This resolved the old clash
  between the `DA_Lights` draw and the `Power.Lights` consumer: the Lights
  part owns both.
  - Test loads go through `AddLoad`/`RemoveLoad` (`Load.<name>`). They are
    not parts.
- **The catalogue is `Tools/ship_parts.json`.** `Tools/setup_ship_parts.py`
  authors `Content/Ship/Parts/DA_*`, `DA_ShipCatalogue` and the game mode's
  list from it (editor closed, through the lock; the report is
  `Saved/setup_ship_parts.txt`). **Never edit a part asset by hand.**
  - `DeepSpace.Ship.Parts.Contract` holds the JSON, the assets and
    `FShipRatings::Stock()` equal.
  - `.CatalogueRules` holds decision 7 over every row:
    - every combination is whole at rest under the stock reactor;
    - every upgrade is at least as open as stock on every axis of its bay;
    - no part in a bay dominates another;
    - aux parts rate nothing and draw nothing at rest.

    **Parts widen what the ship can do; they never make it need more of
    anything.**
  - `FindPart` resolves ids through `DA_ShipCatalogue`, which `CatalogueAsset`
    in `[/Script/DeepSpace.ShipSubsystem]` names.
- **Four CVars override the fitted part.** `ds.Nav.RangeLy`,
  `ds.Nav.ChargeSeconds`, `ds.Nav.WindingWant` and `ds.Drive.Response` default
  to `-1`, the part's. 0 or more overrides it for the session
  (`ShipParts::Effective`, applied only in the ship's getters). Wherever this
  file quotes one of them as a number, read the fitted part's rating.
- **The console's nameplates**: one line per fitted part,
  `BAY  Name  figure  Words`, each figure the part's own. No CVar or live
  value moves a plate, and an empty slot has no line
  (`DeepSpace.Ship.Parts.NameplatesAreFacts`).
- **The state is plain** (`FShipLoadoutState`): bays by name, and spares with
  their own state. The wear fields stay zero until slice 4.
  `RestoreLoadout` falls back to the stock part for whatever it cannot name.
```

- [ ] **Step 5: CLAUDE.md, *Playtest console***

After the paragraph that ends `and \`ds.Dress.Seed\` redress the ship where you stand (*The dressing*).`, add:

````markdown
Parts, from the console, until there is somewhere to find them:

```text
ds.Ship.Install Reactor.TwinCore   fit a part by id or name; ds.Ship.Install Reactor.Stock puts it back
ds.Ship.Install Drive.QuickLever   the drive that follows its lever half as fast again
ds.Ship.Spares                     the spares aboard; 'give <part>' adds one, 'clear' empties them
ds.Ship.Describe                   every bay: its part, draw and ratings (a developer's line)
```
````

- [ ] **Step 6: CLAUDE.md, *Where each tunable lives***

Replace the four rows:

```markdown
| `ds.Nav.ChargeSeconds` | 45 s (settled 2026-09-26) | `ShipSubsystem.cpp`, from `FShipFlightState::JumpChargeSeconds` (`ShipFlightState.h`) |
| `ds.Nav.WindingWant` | 380 W (settled 2026-09-26) | `ShipSubsystem.cpp` |
| `ds.Nav.RangeLy` | 12 ly | `ShipSubsystem.cpp` |
| `ds.Drive.Response` | 3 notches/s at full thrust | `ShipSubsystem.cpp`, from `ShipDriveLever::DefaultResponse` |
```

with, each in its own place:

```markdown
| `ds.Nav.ChargeSeconds` | -1: the drive part's (stock 45 s, settled 2026-09-26) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Nav.WindingWant` | -1: the drive part's (stock 380 W, settled 2026-09-26) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Nav.RangeLy` | -1: the sensors part's (stock 12 ly) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
| `ds.Drive.Response` | -1: the drive part's (stock 3 notches/s at full thrust) | `ShipSubsystem.cpp`; the part's number is in `Tools/ship_parts.json` |
```

In the *Tunables that are not CVars* paragraph, replace:

```text
level rebuild); the reactor rating and each consumer's want
(`UShipSubsystem`'s `static constexpr`s, a header change). And, as named
constants with tests on them: the drive's notch table, `EaseSeconds` 0.4,
`RepeatDelaySeconds` 0.3, `ArriveNotches` and cruise's log floor
`CruiseFloorCmPerSecond` 1 m/s (`ShipDriveLever.*`); cruise's top 20 km/s,
its astern top 200 m/s and the boosters' 2 km/s^2 (`FShipFlightLimits`, a
header change); the 80% braking margin
```

with:

```text
level rebuild); every part's draw and rated values -- the reactor's supply,
each consumer's want, the boosters' 2 km/s^2, the drive's response and
charge, the array's range -- in `Tools/ship_parts.json`, authored into assets
by `Tools/setup_ship_parts.py` (*Parts and bays*). And, as named constants
with tests on them: the drive's notch table, `EaseSeconds` 0.4,
`RepeatDelaySeconds` 0.3, `ArriveNotches` and cruise's log floor
`CruiseFloorCmPerSecond` 1 m/s (`ShipDriveLever.*`); cruise's top 20 km/s and
its astern top 200 m/s (`FShipFlightLimits`, a header change); the 80%
braking margin
```

If that paragraph has changed on `main` since `e5d257c`, make the same two substitutions in whatever it says now:
- "the reactor rating and each consumer's want (`UShipSubsystem`'s `static constexpr`s...)" becomes the parts clause;
- the boosters' 2 km/s^2 moves out of `FShipFlightLimits`'s list.

- [ ] **Step 7: The power state's comment, ADR 0003, the navigation spec's fake, the spec's status**

In `Source/DeepSpace/Ship/ShipPowerState.h`, replace the **Draws** bullet (line 31):

```cpp
 * - **Draws** are installed modules. They take what they take, off the top.
```

with:

```cpp
 * - **Draws** are what a fitted part, a test load or the fold takes off the
 *   top, keyed by bay ("Bay.<Bay>"), "Load.<name>" or "Nav.Fold". They take
 *   what they take.
```

Then replace (lines 38-39):

```cpp
 * Nothing drifts. Weights are a preference the player set and they stay set;
 * no value in here changes on its own with time.
 */
```

with:

```cpp
 * Nothing drifts. Weights are a preference the player set and they stay set;
 * no value in here changes on its own with time.
 *
 * The reactor's output is the reactor part's rating, which UShipSubsystem
 * pushes on every fit (the wear and upgrades spec). A draw is never keyed by
 * part, so a swap can never leave two parts drawing in one bay.
 */
```

Append to `docs/decisions/0003-ship-state-as-subsystem.md`:

```markdown

## Amended 2026-09-27: the ship's parts (wear and upgrades, slice 1)

The ship's parts live in `UShipSubsystem`, over the pure core `Ship/ShipParts.*`: which part
is in each bay, the spares, and (from slice 4) their wear. They are held as one plain
`FShipLoadoutState`: ids, never pointers, and bays by name, never position. It is world-level,
and it is what the save will write (slice 3). The rated values -- the reactor's supply, the
wants, the boosters' acceleration, the drive's response and charge, the chart's range -- are
derived from the fitted parts on every ask and stored nowhere. A fit is a translation of the
loadout, as an arrival is of the position, so no second copy of a rating can disagree with the
part that rates it.
```

In `docs/superpowers/specs/2026-09-25-navigation-and-arrival-design.md`, replace:

```text
- **Jump range is the flat 12 ly chart radius.** *Cost:* making range an
  upgrade means a different number, not a different model.
```

with:

```text
- **Jump range is the flat 12 ly chart radius.** *Cost:* making range an
  upgrade means a different number, not a different model. *Paid* (wear and
  upgrades slice 1, 2026-09-27): the Sensors bay rates `RangeLy`
  (`docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md`).
```

In `docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md`, replace:

```text
*Decisions needing sign-off* as recommended. Not implemented.
```

with:

```text
*Decisions needing sign-off* as recommended. Slice 1 (the upgrade seam)
implemented per `docs/superpowers/plans/2026-09-27-wear-slice-1-upgrade-seam.md`;
slices 2-4 not implemented.
```

- [ ] **Step 8: The whole suite, on the merged tree**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && ./build.sh && ./test.sh
```

Expected: `python: ... files passed`, then `passed: M` with no `FAILED` and no `LOG NOT CLEAN`. M is at least Task 1's N, plus this slice's 14 new tests:
- `DeepSpace.Ship.Parts.Arithmetic`, `.AssetSpec`, `.CatalogueRules`, `.Contract`, `.CVarOverrides`, `.EmptyBayIsStock`, `.OnePerBay`;
- `.WantsFollowTheFit`, `.StockIsToday`, `.RatingsFollowParts`, `.NameplatesAreFacts`, `.CatalogueResolves`, `.Commands`, `.StateRoundTrips`;
- plus whatever tests landing (a) added on `main`.

Compare the tests that ran against those defined: a count lower than expected means a test path became a group.

- [ ] **Step 9: Every Blueprint compiles, and the game mode holds six parts**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && . Tools/ue_lock.sh && \
ue_locked ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_blueprints.py" -unattended -nopause -nosplash -NoLiveCoding; echo "exit $?"; \
strings -a Content/Blueprints/BP_DeepSpaceGameMode.uasset | grep -cE 'DA_(Lights|LifeSupport|Sensors)\b'; \
strings -a Content/Blueprints/BP_DeepSpaceGameMode.uasset | grep -oE 'DA_(Reactor|Drive|Boosters|Lights|LifeSupport|Sensors)_Stock' | sort -u | wc -l
```

Expected: `exit 0`, then `0` (no old module names), then `6`. The headless console checks in slice 1's done-when (`ds.Ship.Install Reactor.TwinCore` gives 1800 W and `Reactor.Stock` 1400 W again; `Drive.QuickLever` gives 4.5 and `Drive.Stock` 3 again) are `DeepSpace.Ship.Parts.Commands`, which Step 8 ran.

- [ ] **Step 10: Commit the documentation**

```bash
cd /home/matt/Development/deepspace/.worktrees/wear-1-s && \
git add CLAUDE.md Source/DeepSpace/Ship/ShipPowerState.h docs/decisions/0003-ship-state-as-subsystem.md \
        docs/superpowers/specs/2026-09-25-navigation-and-arrival-design.md docs/superpowers/specs/2026-09-27-ship-wear-and-upgrades-design.md && \
git commit -m "$(cat <<'EOF'
docs: parts and bays -- CLAUDE.md, the tunables, ADR 0003 amended, the range fake paid

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 11: Merge to main (orchestrator)**

```bash
cd /home/matt/Development/deepspace && git status --short --untracked-files=no && git merge --no-ff feat/wear-1-s -m "$(cat <<'EOF'
merge: wear and upgrades slice 1 -- the upgrade seam

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
EOF
)" && ./build.sh && ./test.sh
```

Expected: an empty status (a modified or staged file on `main` stops here; untracked files, such as other plans not yet committed, are not listed and are not touched by the merge). Then a clean merge, `Result: Succeeded`, and `passed: M` or more with no `FAILED`. Landing Task 13 (B0) Step 0a now passes. Tell the orchestrator of landing slice (b) that it may open.

---

## Self-review against the spec (slice 1)

| Spec requirement (slice 1) | Task |
|---|---|
| `EShipBay` with `None` first; one part per bay; a swap makes a spare (decision 1) | 2, 7 |
| Ratings per bay, stock is today's, no part rates a top (decision 2); callers of the statics move (track S list) | 2, 6 |
| An empty bay reads stock; core parts only swapped (decision 3) | 7 |
| Lights clash: one part, draws by bay, ids `<Bay>.<Name>`, consumers unchanged (decision 4) | 2, 5, 7 |
| JSON contract, authoring script, `DA_ShipCatalogue`, ini line, `FindPart` in a bare world (decision 5) | 4, 5, 10 |
| Four CVars at -1, `Effective`, tests that read them raw move (decision 6) | 2, 6 |
| Decision 7's rules over the catalogue | 4 |
| `.CatalogueRules`' "no two aux parts alike" | **deferred** to the first aux part's spec: aux parts rate and draw nothing, and slice 1 has no verb field, so "alike" has no definition yet; a second aux row under a known id is already refused (*Global Constraints*) |
| *Risks*: the grep for renamed `ModuleId`s | 8 (Step 1) |
| The console's DRAWN and SPARE removed; the HUD's `SPARE` (not named by the spec) | 9; the HUD line is kept and put to the developer (*Out of scope*) |
| Aux rules: structural at fit, the rest over the catalogue; `AddLoad` for the hogs; `InstallModule` retired (decision 8, sign-off 28) | 4, 7, 8 |
| Nameplates; DRAWN and SPARE removed; `GetReadout` kept; ScreensAgree moved (decision 9, sign-offs 10, 11) | 9 |
| `ds.Ship.Install`, `ds.Ship.Spares`, `ds.Ship.Describe`; a spare first (decision 10) | 10, 11 |
| Plain state; round trip; restore by name (decision 11) | 2, 12 |
| The twin core and the quick lever (sign-offs 5, 24) | 4, 5, 8 |
| The landing plan amended; one writer of the boosters' want (sign-off 29) | 6, 13 |
| Documentation: CLAUDE.md, the power header, ADR 0003, the range fake | 14 |
| Done when: the suite, `check_blueprints.py`, `strings` on the game mode, the console checks, every test mutate-proven | 5, 8, 9, 14 and each task's last step |
