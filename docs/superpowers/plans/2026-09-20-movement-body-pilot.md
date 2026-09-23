# Movement, Body, and Pilot Seat Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the player sprint and crouch, an animated full body the camera rides, and a pilot seat that puts the ship in pilot mode.

**Architecture:** Movement rules and posture are decided in C++ (`FMovementRules`, `ADeepSpaceCharacter::GetPosture`); a C++ `UDeepSpaceAnimInstance` exposes them, and a wiring-only Animation Blueprint turns them into poses. Mixamo clips are imported and retargeted onto the mannequin by an editor Python script, which also builds the blend spaces through a small C++ editor helper. The pilot seat is a C++ actor the level builder places; it records the pilot on `UShipSubsystem`. A single JSON file carries the ship↔character movement contract, read by both the Python layout and a C++ test.

**Tech Stack:** Unreal Engine 5.8 C++ (Enhanced Input, CharacterMovement, AnimInstance, automation tests), Python 3 (bare, and the editor's `PythonScriptPlugin`), the IK Rig retargeting plugin, Mixamo FBX sources.

**Spec:** `docs/superpowers/specs/2026-09-20-movement-body-pilot-design.md` — read its *Amendments after the retarget prototype* section: the crouch and crawlway sizes there supersede the earlier sections.

## Global Constraints

- **All gameplay logic in C++.** Blueprints assign assets; `ABP_DeepSpaceBody` may contain only: output pose, blend-space players, sequence players, *Blend Poses by* nodes, variable *gets*, and the default event stubs. Checked by `Tools/check_anim_blueprints.py`.
- Movement contract (`Tools/movement_contract.json`): **stand clearance 180**, **crouch clearance 140**, **capsule radius 34** (cm).
- Character: **walk 300**, **sprint 600**, **crouch 150** cm/s; crouched capsule **half-height 65** (130 cm); standing capsule half-height **88** (engine default); radius **34** (engine default).
- Crawlway **140 cm** tall with **90 × 140** doors.
- Sprint: **hold Left Shift**, only moving forward-dominant and not crouched. Crouch: **C toggles**.
- Seated look limits: **±100° yaw**, **±70° pitch** around the seat's facing.
- The camera rides the **`head`** bone through a zero-length spring arm (lag speed 15, forward offset 8 cm); the head bone is hidden from the owner.
- **Every C++ change: close the editor, then `./rebuild.sh`.** Linux has no Live Coding; `./build.sh` with the editor open only makes hot-reload libraries.
- Editor-side scripts (`UnrealEditor-Cmd -run=pythonscript`) write results to `Saved/`; `unreal.log` does not reach stdout.
- Never run an editor-side script while the Unreal editor is open (`pgrep -f "[U]nrealEditor .*DeepSpace"`).

## Deviations from the spec

| Spec said | Plan does | Why |
|---|---|---|
| Blend spaces built by the import script | Built by the script **plus** a C++ helper, `UDeepSpaceEditorScripting::RebuildBlendSpace` | Setting a blend space's samples from Python stores them but never builds the interpolation table the engine blends from; verified by comparing serialised assets. Without the helper, the body would hold its idle pose at every speed. |
| `BP_DeepSpaceCharacter` assignments and the input actions by hand | Scripted: `Tools/setup_character.py` | Prototyped: Blueprint defaults set from Python persist across a fresh process. The only hand work left is the six-node anim graph. |
| *Stand To Sit* / *Sit To Stand* play when sitting | Imported and retargeted, **unused this round**; posture changes blend over 0.25 s | Playing them needs either a state machine (forbidden) or C++-driven montages; the latter is a clean follow-up, not needed to sit. |
| — | Seated pose is `RTG_sitting_idle`, not `seated_idle` | Measured: it chains exactly with the transitions (head 119 cm); `seated_idle` sits 7 cm higher. |
| The camera rides the head on a zero-length spring arm with lag | `ADeepSpaceCharacter::PlaceCamera` places the camera in C++; no spring arm | The first playtest failed three ways, all from letting the animation decide where the camera is. (a) The lag is positional in every axis, so under the sprint lean the body ran ahead of the camera and came into frame. (b) A spring arm applies `SocketOffset` in the *view's* rotation, so looking straight down swung the 8 cm forward offset downwards and dropped the camera into the neck — the body was seen from the inside, and past it. (c) Nothing kept the camera in the capsule: the retargeted crouch idle carries the head **57 cm** from the axis against a **34 cm** capsule, so crouching against a wall put the view outside the hull. The arm's own collision test would have caught (c), but the engine skips it when `TargetArmLength == 0`. `PlaceCamera` is rigid sideways, damps only the vertical bob, applies the forward offset in yaw only, and sweeps out from the capsule's axis. `DeepSpace.Player.CameraStaysInsideWalls` and `.CameraDoesNotDiveWhenLookingDown` pin all three. |
| — | `rebuild.sh` fixed before this plan | It failed after every successful build (`bc` missing; `ls` under `pipefail`), which would have made `launch.sh` refuse to open the editor after any C++ change. Committed as `02d775b`. |

## File Structure

| File | Responsibility | Task |
|---|---|---|
| `Tools/movement_contract.json` | The three shared numbers | 1 |
| `Tools/hauler_layout.py` | Reads the contract; 140 cm crawlway; later the pilot seat | 1, 7 |
| `Source/DeepSpace/Player/MovementRules.h/.cpp` | Speeds, crouched height, `CanSprint` — pure | 2 |
| `Source/DeepSpace/Player/Posture.h` | `EPosture` | 2 |
| `Source/DeepSpace/Tests/MovementTest.cpp` | `DeepSpace.Player.Sprint`, later `.MovementContract` | 2, 4 |
| `Source/DeepSpace/Ship/ShipSubsystem.h/.cpp` | `SetPilot` / `ClearPilot` / `IsPiloted` / `GetPilot` | 3 |
| `Source/DeepSpace/Tests/ShipPilotTest.cpp` | `DeepSpace.Ship.Piloted` | 3 |
| `Source/DeepSpace/Ship/PilotSeat.h/.cpp` | The helm actor | 4 |
| `Source/DeepSpace/Player/DeepSpaceCharacter.h/.cpp` | Sprint, crouch, body camera, sitting | 4 |
| `Source/DeepSpace/Player/DeepSpaceAnimInstance.h/.cpp` | Speed and posture for the anim graph | 4 |
| `Source/DeepSpace/DeepSpace.Build.cs` | `Json` dependency | 4 |
| `Source/DeepSpace/Editor/DeepSpaceEditorScripting.h/.cpp` | `RebuildBlendSpace` | 5 |
| `Tools/import_animations.py` | FBX import, IK rigs, retarget, blend spaces | 5 |
| `Tools/setup_character.py` | Input actions, ABP shell, Blueprint defaults | 6 |
| `Tools/check_anim_heights.py` | Camera-inside-capsule guard | 6 |
| `Tools/check_anim_blueprints.py` | Wiring-only guard | 6 |
| `Tools/build_hauler.py`, `Tools/verify_level.py` | Place and verify the pilot seat | 7 |
| `docs/…` , `CLAUDE.md` | ADR 0002 amendment, pipeline, checklist | 8 |

---

### Task 1: The movement contract as one file, and the raised crawlway

Pure Python; no editor.

**Files:**
- Create: `Tools/movement_contract.json`
- Modify: `Tools/hauler_layout.py`

**Interfaces:**
- Produces: `Tools/movement_contract.json` with keys `stand_clearance`, `crouch_clearance`, `capsule_radius` (numbers, cm). `hauler_layout.STAND_CLEARANCE`, `CROUCH_CLEARANCE`, `CAPSULE_RADIUS` keep their names, now loaded from it.

- [ ] **Step 1: Create the contract file**

`Tools/movement_contract.json`:

```json
{
    "_comment": "The ship and the character must agree on these. Read by Tools/hauler_layout.py and by the C++ test DeepSpace.Player.MovementContract. Centimetres.",
    "stand_clearance": 180,
    "crouch_clearance": 140,
    "capsule_radius": 34
}
```

- [ ] **Step 2: Make the layout read it**

In `Tools/hauler_layout.py`, change the imports from

```python
import os
import sys
```

to

```python
import json
import os
import sys
```

and replace the contract block

```python
# -- The contract with the character -------------------------------------
# The ship is built for these. The character's standing capsule must be
# shorter than STAND_CLEARANCE, and its crouched capsule shorter than
# CROUCH_CLEARANCE and than the crawlway's doors, or the crawlway becomes
# impassable while this layout validates as fine.
STAND_CLEARANCE = 180
CROUCH_CLEARANCE = 90
CAPSULE_RADIUS = 34     # the default character capsule; the 10 cm grid rounds it to 3 cells
```

with

```python
# -- The contract with the character -------------------------------------
# The ship is built for these, and the character is built to fit them. They
# live in movement_contract.json because both sides read them: this file, and
# the C++ test DeepSpace.Player.MovementContract, which fails if the
# character's capsules stop fitting. Change them there, never here.
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "movement_contract.json")) as _f:
    _CONTRACT = json.load(_f)
STAND_CLEARANCE = _CONTRACT["stand_clearance"]
CROUCH_CLEARANCE = _CONTRACT["crouch_clearance"]
CAPSULE_RADIUS = _CONTRACT["capsule_radius"]   # the 10 cm grid rounds 34 to 3 cells
```

- [ ] **Step 3: Raise the crawlway**

Replace

```python
    Room("crawlway",    0,    390,  390,  90,  110),
```

with

```python
    # 140 cm: the retargeted crouch-walk clip carries the head bone to 118 cm,
    # and the camera rides it, so a lower ceiling would be seen through.
    Room("crawlway",    0,    390,  390,  90,  140),
```

and replace

```python
    # The crawlway's full 90 cm width: the open end of a service duct.
    Door("cargo_bay", "crawlway", 90, 100),
    Door("crawlway", "engineering", 90, 100),
```

with

```python
    # The crawlway's full width and height: the open end of a service duct.
    Door("cargo_bay", "crawlway", 90, 140),
    Door("crawlway", "engineering", 90, 140),
```

- [ ] **Step 4: Validate, and confirm the crouch-only check still bites**

```bash
python3 Tools/validate_hauler.py | tail -1
python3 Tools/test_floorplan.py | tail -1
python3 Tools/test_placement.py | tail -1
cp Tools/hauler_layout.py /tmp/claude-1000/layout.bak
sed -i 's|Room("crawlway",    0,    390,  390,  90,  140)|Room("crawlway",    0,    390,  390,  90,  190)|; s|90, 140),|90, 185),|g' Tools/hauler_layout.py
python3 Tools/validate_hauler.py | grep -E "^  - "
cp /tmp/claude-1000/layout.bak Tools/hauler_layout.py
python3 Tools/validate_hauler.py | tail -1
```

Expected: `PASS: …crawlway crouch-only…`, `17 passed`, `12 passed`; then the mutation prints `Region 'crawlway' at (195, 435) can be reached standing: it no longer requires crouching.`; then `PASS` again.

- [ ] **Step 5: Commit**

```bash
git add Tools/movement_contract.json Tools/hauler_layout.py
git commit -m "feat: movement contract as one shared file; raise the crawlway to 140 cm"
```

---

### Task 2: Movement rules and posture, test-first

Pure C++ with no world, like `FShipPowerState`. **Close the editor before building.**

**Files:**
- Create: `Source/DeepSpace/Player/MovementRules.h`, `Source/DeepSpace/Player/MovementRules.cpp`, `Source/DeepSpace/Player/Posture.h`, `Source/DeepSpace/Tests/MovementTest.cpp`

**Interfaces:**
- Produces: `FMovementRules::{WalkSpeed, SprintSpeed, CrouchSpeed, CrouchedHalfHeight}` (`static constexpr float`); `static bool FMovementRules::CanSprint(bool bIsCrouched, const FVector2D& MoveInput)` — `MoveInput.X` strafes right, `.Y` moves forward. `UENUM(BlueprintType) enum class EPosture : uint8 { Standing, Crouched, Seated, Falling }`.

- [ ] **Step 1: Write the failing test**

Create `Source/DeepSpace/Tests/MovementTest.cpp` with only the sprint test for now (Task 4 adds the contract test):

```cpp
#include "Misc/AutomationTest.h"
#include "Player/MovementRules.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSprintRuleTest,
    "DeepSpace.Player.Sprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSprintRuleTest::RunTest(const FString& Parameters)
{
    // IA_Move: X strafes right, Y moves forward.
    TestTrue(TEXT("straight ahead sprints"), FMovementRules::CanSprint(false, FVector2D(0.0f, 1.0f)));
    TestTrue(TEXT("forward-diagonal sprints"), FMovementRules::CanSprint(false, FVector2D(0.7f, 0.7f)));
    TestFalse(TEXT("pure strafe walks"), FMovementRules::CanSprint(false, FVector2D(1.0f, 0.0f)));
    TestFalse(TEXT("strafe-dominant walks"), FMovementRules::CanSprint(false, FVector2D(0.9f, 0.4f)));
    TestFalse(TEXT("backwards walks"), FMovementRules::CanSprint(false, FVector2D(0.0f, -1.0f)));
    TestFalse(TEXT("standing still walks"), FMovementRules::CanSprint(false, FVector2D::ZeroVector));
    TestFalse(TEXT("a sliver of forward walks"), FMovementRules::CanSprint(false, FVector2D(0.0f, 0.05f)));
    TestFalse(TEXT("crouched never sprints"), FMovementRules::CanSprint(true, FVector2D(0.0f, 1.0f)));
    return true;
}

#endif
```

- [ ] **Step 2: Build to see it fail**

Run: `./rebuild.sh 2>&1 | grep -E "error|Result:"`
Expected: a compile error — `Player/MovementRules.h` not found.

- [ ] **Step 3: Write the rules and the posture enum**

`Source/DeepSpace/Player/MovementRules.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * Pure movement rules and tuning, with no Unreal types beyond maths. Kept out
 * of the character so they can be unit-tested without a world, the same
 * reason FShipPowerState is separate from UShipSubsystem.
 */
struct DEEPSPACE_API FMovementRules
{
    /** Ground speeds, cm/s. */
    static constexpr float WalkSpeed = 300.0f;
    static constexpr float SprintSpeed = 600.0f;
    static constexpr float CrouchSpeed = 150.0f;

    /**
     * Crouched capsule half-height, cm: 130 cm tall. Sized so the retargeted
     * crouch-walk clip, whose head bone peaks at 118 cm, keeps the camera
     * inside the capsule. Must stay under the ship's crouch clearance in
     * Tools/movement_contract.json; DeepSpace.Player.MovementContract checks.
     */
    static constexpr float CrouchedHalfHeight = 65.0f;

    /**
     * Whether the player may sprint. MoveInput is IA_Move's value: X strafes
     * right, Y moves forward. Sprinting needs the forward component to be
     * positive and to dominate, so strafing and backing up stay at walking
     * pace, and crouching always does.
     */
    static bool CanSprint(bool bIsCrouched, const FVector2D& MoveInput);
};
```

`Source/DeepSpace/Player/MovementRules.cpp`:

```cpp
#include "Player/MovementRules.h"

bool FMovementRules::CanSprint(bool bIsCrouched, const FVector2D& MoveInput)
{
    if (bIsCrouched)
    {
        return false;
    }
    // A small dead zone, so resting fingers on W and a strafe key read as
    // strafing rather than sprinting.
    constexpr float MinForward = 0.1f;
    return MoveInput.Y > MinForward && MoveInput.Y >= FMath::Abs(MoveInput.X);
}
```

`Source/DeepSpace/Player/Posture.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Posture.generated.h"

/**
 * What the player's body is doing, decided in C++ by the character. The
 * animation blueprint only reads it -- its single "Blend Poses by EPosture"
 * node picks a pose per value -- so the decision never lives in Content/.
 */
UENUM(BlueprintType)
enum class EPosture : uint8
{
    Standing,
    Crouched,
    Seated,
    Falling,
};
```

- [ ] **Step 4: Build and run the tests**

```bash
./rebuild.sh 2>&1 | grep -E "error|Result:"
rm -f Saved/Logs/DeepSpace.log
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace" -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding >/dev/null 2>&1
grep "Test Completed" Saved/Logs/DeepSpace.log
```

Expected: `Result: Succeeded`; `Result={Success}` for `DeepSpace.Player.Sprint` and `DeepSpace.Ship.PowerState`.

- [ ] **Step 5: Commit**

```bash
git add Source/DeepSpace/Player/MovementRules.h Source/DeepSpace/Player/MovementRules.cpp Source/DeepSpace/Player/Posture.h Source/DeepSpace/Tests/MovementTest.cpp
git commit -m "feat: pure movement rules and posture enum, with sprint test"
```

---

### Task 3: The ship knows who is piloting

**Close the editor before building.**

**Files:**
- Modify: `Source/DeepSpace/Ship/ShipSubsystem.h`, `Source/DeepSpace/Ship/ShipSubsystem.cpp`
- Create: `Source/DeepSpace/Tests/ShipPilotTest.cpp`

**Interfaces:**
- Produces: `void UShipSubsystem::SetPilot(APawn*)`, `void ClearPilot()`, `bool IsPiloted() const`, `APawn* GetPilot() const` — all `UFUNCTION`s. The pilot is held weakly.

- [ ] **Step 1: Write the failing test**

`Source/DeepSpace/Tests/ShipPilotTest.cpp`:

```cpp
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPilotedTest,
    "DeepSpace.Ship.Piloted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPilotedTest::RunTest(const FString& Parameters)
{
    // A throwaway world: world subsystems are created with it, which is the
    // only way to get a real UShipSubsystem.
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("PilotedTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    if (TestNotNull(TEXT("the world has a ship subsystem"), Ship))
    {
        TestFalse(TEXT("a new ship is not piloted"), Ship->IsPiloted());

        APawn* Pilot = World->SpawnActor<APawn>();
        TestNotNull(TEXT("a pilot pawn spawns"), Pilot);
        Ship->SetPilot(Pilot);
        TestTrue(TEXT("seating a pilot pilots the ship"), Ship->IsPiloted());
        TestEqual(TEXT("the ship knows who is flying"), Ship->GetPilot(), Pilot);

        Ship->ClearPilot();
        TestFalse(TEXT("vacating the seat un-pilots the ship"), Ship->IsPiloted());

        // The subsystem holds its pilot weakly: a destroyed pawn is no pilot.
        Ship->SetPilot(Pilot);
        Pilot->Destroy();
        CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
        TestFalse(TEXT("a destroyed pilot does not keep the ship piloted"), Ship->IsPiloted());
    }

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
```

- [ ] **Step 2: Build to see it fail**

Run: `./rebuild.sh 2>&1 | grep -E "error|Result:"`
Expected: compile errors — `SetPilot`, `IsPiloted`, `GetPilot`, `ClearPilot` are not members of `UShipSubsystem`.

- [ ] **Step 3: Add the seam**

In `Source/DeepSpace/Ship/ShipSubsystem.h`, change

```cpp
class UShipModuleDataAsset;
```

to

```cpp
class APawn;
class UShipModuleDataAsset;
```

after `bool IsPowerOverloaded() const;` (and its `UFUNCTION` line) add

```cpp

    /**
     * Pilot mode. The pilot seat reports who sits at the helm; anything that
     * cares whether the ship is being flown asks here rather than reaching
     * into the seat or the character.
     */
    UFUNCTION(BlueprintCallable, Category = "Ship")
    void SetPilot(APawn* NewPilot);

    UFUNCTION(BlueprintCallable, Category = "Ship")
    void ClearPilot();

    UFUNCTION(BlueprintPure, Category = "Ship")
    bool IsPiloted() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    APawn* GetPilot() const;
```

and after `FShipPowerState PowerState;` add

```cpp

    /** Weak: the subsystem must not keep a pawn alive. */
    TWeakObjectPtr<APawn> Pilot;
```

In `Source/DeepSpace/Ship/ShipSubsystem.cpp`, after `#include "Engine/World.h"` add `#include "GameFramework/Pawn.h"`, and append:

```cpp

void UShipSubsystem::SetPilot(APawn* NewPilot)
{
    Pilot = NewPilot;
}

void UShipSubsystem::ClearPilot()
{
    Pilot.Reset();
}

bool UShipSubsystem::IsPiloted() const
{
    return Pilot.IsValid();
}

APawn* UShipSubsystem::GetPilot() const
{
    return Pilot.Get();
}
```

- [ ] **Step 4: Build and run the tests**

As Task 2 Step 4. Expected: `Result={Success}` for `DeepSpace.Ship.Piloted`, `DeepSpace.Player.Sprint`, `DeepSpace.Ship.PowerState`.

- [ ] **Step 5: Commit**

```bash
git add Source/DeepSpace/Ship/ShipSubsystem.h Source/DeepSpace/Ship/ShipSubsystem.cpp Source/DeepSpace/Tests/ShipPilotTest.cpp
git commit -m "feat: UShipSubsystem records who is piloting"
```

---

### Task 4: The character — sprint, crouch, a body the camera rides, and the pilot seat

The character and the seat reference each other, so they land together. **Close the editor before building.**

**Files:**
- Create: `Source/DeepSpace/Ship/PilotSeat.h/.cpp`, `Source/DeepSpace/Player/DeepSpaceAnimInstance.h/.cpp`
- Modify (full replace): `Source/DeepSpace/Player/DeepSpaceCharacter.h`, `Source/DeepSpace/Player/DeepSpaceCharacter.cpp`
- Modify: `Source/DeepSpace/DeepSpace.Build.cs`, `Source/DeepSpace/Tests/MovementTest.cpp`

**Interfaces:**
- Consumes: `FMovementRules`, `EPosture` (Task 2); `UShipSubsystem::SetPilot/ClearPilot` (Task 3).
- Produces:
  - `APilotSeat` (Python: `unreal.PilotSeat`): `FTransform GetSeatTransform() const`, `FVector GetExitLocation() const`; spawned at floor level, facing = seat facing.
  - `ADeepSpaceCharacter`: `EPosture GetPosture() const` (`BlueprintPure`), `void SitIn(APilotSeat*)`, `void StandUp()`, `bool IsSeated() const`; new `EditDefaultsOnly` properties `SprintAction`, `CrouchAction` (Python: `sprint_action`, `crouch_action`); components `CameraArm` (spring arm on the mesh's `head` bone) and `FirstPersonCamera` (unchanged name).
  - `UDeepSpaceAnimInstance` (Python: `unreal.DeepSpaceAnimInstance`): `BlueprintReadOnly float Speed`, `EPosture Posture`.

- [ ] **Step 1: Write the failing contract test**

In `Source/DeepSpace/Tests/MovementTest.cpp`, replace the includes at the top with:

```cpp
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/DeepSpaceCharacter.h"
#include "Player/MovementRules.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
```

and insert, directly before the closing `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMovementContractTest,
    "DeepSpace.Player.MovementContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** The character as the game uses it: the Blueprint if it loads, else the C++ class. */
    const ADeepSpaceCharacter* PlayableCharacterDefaults(FString& OutWhich)
    {
        const UClass* Blueprint = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        OutWhich = Blueprint ? TEXT("BP_DeepSpaceCharacter") : TEXT("ADeepSpaceCharacter");
        return Blueprint ? Blueprint->GetDefaultObject<ADeepSpaceCharacter>()
                         : GetDefault<ADeepSpaceCharacter>();
    }
}

bool FMovementContractTest::RunTest(const FString& Parameters)
{
    // The ship's side of the contract, the same file the level validator reads.
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/movement_contract.json"));
    FString Text;
    if (!TestTrue(TEXT("contract file loads: ") + Path, FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Contract;
    if (!TestTrue(TEXT("contract parses as JSON"),
                  FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Contract) && Contract.IsValid()))
    {
        return false;
    }
    const double StandClearance = Contract->GetNumberField(TEXT("stand_clearance"));
    const double CrouchClearance = Contract->GetNumberField(TEXT("crouch_clearance"));
    const double Radius = Contract->GetNumberField(TEXT("capsule_radius"));

    FString Which;
    const ADeepSpaceCharacter* Character = PlayableCharacterDefaults(Which);
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const double Standing = 2.0 * Capsule->GetUnscaledCapsuleHalfHeight();
    const double Crouched = 2.0 * Character->GetCharacterMovement()->GetCrouchedHalfHeight();

    AddInfo(FString::Printf(TEXT("%s: standing %.0f, crouched %.0f, radius %.0f; ship: stand %.0f, crouch %.0f, radius %.0f"),
        *Which, Standing, Crouched, Capsule->GetUnscaledCapsuleRadius(), StandClearance, CrouchClearance, Radius));

    TestTrue(TEXT("standing capsule fits under the stand clearance"), Standing < StandClearance);
    TestTrue(TEXT("crouched capsule fits under the crouch clearance"), Crouched < CrouchClearance);
    TestTrue(TEXT("crouched capsule is too tall to stand up in a crouch space"), Standing > CrouchClearance);
    TestEqual(TEXT("capsule radius matches the contract"), (double)Capsule->GetUnscaledCapsuleRadius(), Radius);
    return true;
}

```

The full file after this step must equal:

```cpp
#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/DeepSpaceCharacter.h"
#include "Player/MovementRules.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSprintRuleTest,
    "DeepSpace.Player.Sprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSprintRuleTest::RunTest(const FString& Parameters)
{
    // IA_Move: X strafes right, Y moves forward.
    TestTrue(TEXT("straight ahead sprints"), FMovementRules::CanSprint(false, FVector2D(0.0f, 1.0f)));
    TestTrue(TEXT("forward-diagonal sprints"), FMovementRules::CanSprint(false, FVector2D(0.7f, 0.7f)));
    TestFalse(TEXT("pure strafe walks"), FMovementRules::CanSprint(false, FVector2D(1.0f, 0.0f)));
    TestFalse(TEXT("strafe-dominant walks"), FMovementRules::CanSprint(false, FVector2D(0.9f, 0.4f)));
    TestFalse(TEXT("backwards walks"), FMovementRules::CanSprint(false, FVector2D(0.0f, -1.0f)));
    TestFalse(TEXT("standing still walks"), FMovementRules::CanSprint(false, FVector2D::ZeroVector));
    TestFalse(TEXT("a sliver of forward walks"), FMovementRules::CanSprint(false, FVector2D(0.0f, 0.05f)));
    TestFalse(TEXT("crouched never sprints"), FMovementRules::CanSprint(true, FVector2D(0.0f, 1.0f)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMovementContractTest,
    "DeepSpace.Player.MovementContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** The character as the game uses it: the Blueprint if it loads, else the C++ class. */
    const ADeepSpaceCharacter* PlayableCharacterDefaults(FString& OutWhich)
    {
        const UClass* Blueprint = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        OutWhich = Blueprint ? TEXT("BP_DeepSpaceCharacter") : TEXT("ADeepSpaceCharacter");
        return Blueprint ? Blueprint->GetDefaultObject<ADeepSpaceCharacter>()
                         : GetDefault<ADeepSpaceCharacter>();
    }
}

bool FMovementContractTest::RunTest(const FString& Parameters)
{
    // The ship's side of the contract, the same file the level validator reads.
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/movement_contract.json"));
    FString Text;
    if (!TestTrue(TEXT("contract file loads: ") + Path, FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Contract;
    if (!TestTrue(TEXT("contract parses as JSON"),
                  FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Contract) && Contract.IsValid()))
    {
        return false;
    }
    const double StandClearance = Contract->GetNumberField(TEXT("stand_clearance"));
    const double CrouchClearance = Contract->GetNumberField(TEXT("crouch_clearance"));
    const double Radius = Contract->GetNumberField(TEXT("capsule_radius"));

    FString Which;
    const ADeepSpaceCharacter* Character = PlayableCharacterDefaults(Which);
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const double Standing = 2.0 * Capsule->GetUnscaledCapsuleHalfHeight();
    const double Crouched = 2.0 * Character->GetCharacterMovement()->GetCrouchedHalfHeight();

    AddInfo(FString::Printf(TEXT("%s: standing %.0f, crouched %.0f, radius %.0f; ship: stand %.0f, crouch %.0f, radius %.0f"),
        *Which, Standing, Crouched, Capsule->GetUnscaledCapsuleRadius(), StandClearance, CrouchClearance, Radius));

    TestTrue(TEXT("standing capsule fits under the stand clearance"), Standing < StandClearance);
    TestTrue(TEXT("crouched capsule fits under the crouch clearance"), Crouched < CrouchClearance);
    TestTrue(TEXT("crouched capsule is too tall to stand up in a crouch space"), Standing > CrouchClearance);
    TestEqual(TEXT("capsule radius matches the contract"), (double)Capsule->GetUnscaledCapsuleRadius(), Radius);
    return true;
}

#endif
```

- [ ] **Step 2: Add the Json dependency**

In `Source/DeepSpace/DeepSpace.Build.cs`, replace

```csharp
		PrivateDependencyModuleNames.AddRange(new string[] {  });
```

with

```csharp
		// Json: the movement-contract test reads Tools/movement_contract.json.
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
```

- [ ] **Step 3: Build and run the contract test against today's character**

As Task 2 Step 4. Expected: build succeeds and `DeepSpace.Player.MovementContract` **passes** with `BP_DeepSpaceCharacter: standing 176, crouched 80, radius 34`. There is no red here, honestly: the engine's default crouch (80 cm) already fits the 140 cm clearance. What this step proves is that the test reads the real Blueprint and the real contract file. The red is Step 5's mutation, once the crouch is 130 cm.

- [ ] **Step 4: Write the seat, the anim instance, and the character**

`Source/DeepSpace/Ship/PilotSeat.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PilotSeat.generated.h"

class UBoxComponent;
class UInteractableComponent;
class USceneComponent;

/**
 * The helm. Sitting here puts the ship in pilot mode: the seat tells
 * UShipSubsystem who is piloting, and everything else asks the subsystem.
 *
 * Carries no mesh of its own: the level builder places it over the
 * generated pilot-seat prop. Its box exists to catch the player's reach trace
 * -- it envelops the prop, so the trace hits the seat rather than the
 * decorative chair beneath it, which has no interactable.
 */
UCLASS()
class DEEPSPACE_API APilotSeat : public AActor
{
    GENERATED_BODY()

public:
    APilotSeat();

    /** Where the seated character stands its feet, facing the way it sits. */
    FTransform GetSeatTransform() const;

    /** Where the character is put when it stands up. */
    FVector GetExitLocation() const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<USceneComponent> Root;

    /** Blocks only the Visibility channel: the reach trace, not movement. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<UBoxComponent> ReachVolume;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<USceneComponent> SeatAnchor;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<USceneComponent> ExitPoint;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Seat")
    TObjectPtr<UInteractableComponent> Interactable;

private:
    UFUNCTION()
    void HandleInteracted(AActor* InteractInstigator);
};
```

`Source/DeepSpace/Ship/PilotSeat.cpp`:

```cpp
#include "Ship/PilotSeat.h"

#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Player/DeepSpaceCharacter.h"
#include "Ship/InteractableComponent.h"

APilotSeat::APilotSeat()
{
    PrimaryActorTick.bCanEverTick = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    // Sized to envelop the pilot_seat prop (60 x 60, back to 125 cm), with a
    // little margin so the trace meets this box before the prop's surfaces.
    ReachVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ReachVolume"));
    ReachVolume->SetupAttachment(Root);
    ReachVolume->SetBoxExtent(FVector(40.0f, 35.0f, 65.0f));
    ReachVolume->SetRelativeLocation(FVector(0.0f, 0.0f, 65.0f));
    ReachVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ReachVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    ReachVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

    SeatAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("SeatAnchor"));
    SeatAnchor->SetupAttachment(Root);

    // Behind the seat: the prop's back reaches 30 cm aft of its centre.
    ExitPoint = CreateDefaultSubobject<USceneComponent>(TEXT("ExitPoint"));
    ExitPoint->SetupAttachment(Root);
    ExitPoint->SetRelativeLocation(FVector(-80.0f, 0.0f, 0.0f));

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "PilotSeat", "Pilot Seat");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "SitIn", "Sit in");
}

void APilotSeat::BeginPlay()
{
    Super::BeginPlay();
    Interactable->OnInteracted.AddDynamic(this, &APilotSeat::HandleInteracted);
}

void APilotSeat::HandleInteracted(AActor* InteractInstigator)
{
    if (ADeepSpaceCharacter* Character = Cast<ADeepSpaceCharacter>(InteractInstigator))
    {
        Character->SitIn(this);
    }
}

FTransform APilotSeat::GetSeatTransform() const
{
    return SeatAnchor->GetComponentTransform();
}

FVector APilotSeat::GetExitLocation() const
{
    return ExitPoint->GetComponentLocation();
}
```

`Source/DeepSpace/Player/DeepSpaceAnimInstance.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Player/Posture.h"
#include "DeepSpaceAnimInstance.generated.h"

/**
 * Computes everything the body's animation depends on. ABP_DeepSpaceBody
 * subclasses this and only wires these values into blend spaces: all the
 * deciding happens here, where it can be read, diffed and reviewed.
 */
UCLASS()
class DEEPSPACE_API UDeepSpaceAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

protected:
    /** Ground speed in cm/s; drives the locomotion and crouch blend spaces. */
    UPROPERTY(BlueprintReadOnly, Category = "Body")
    float Speed = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Body")
    EPosture Posture = EPosture::Standing;
};
```

`Source/DeepSpace/Player/DeepSpaceAnimInstance.cpp`:

```cpp
#include "Player/DeepSpaceAnimInstance.h"

#include "Player/DeepSpaceCharacter.h"

void UDeepSpaceAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // The editor's animation preview has no character: leave the defaults.
    const ADeepSpaceCharacter* Character = Cast<ADeepSpaceCharacter>(TryGetPawnOwner());
    if (!Character)
    {
        return;
    }
    Speed = Character->GetVelocity().Size2D();
    Posture = Character->GetPosture();
}
```

Replace the whole of `Source/DeepSpace/Player/DeepSpaceCharacter.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Player/Posture.h"
#include "DeepSpaceCharacter.generated.h"

class APilotSeat;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UInteractableComponent;
class UUserWidget;
struct FInputActionValue;

/**
 * First-person pawn with a body. Owns movement (walk, sprint, crouch), the
 * camera -- which rides the body's head -- the trace that finds the
 * interactable the player is looking at, and sitting in the pilot seat.
 */
UCLASS()
class DEEPSPACE_API ADeepSpaceCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ADeepSpaceCharacter();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    /** The interactable currently under the crosshair, or nullptr. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    UInteractableComponent* GetFocusedInteractable() const;

    /** Prompt for the focused interactable, "Stand up" while seated, or empty. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FText GetCurrentPrompt() const;

    /** What the body is doing. Decided here; the animation only reads it. */
    UFUNCTION(BlueprintPure, Category = "Movement")
    EPosture GetPosture() const;

    /** Sit at the helm: movement off, camera limited, the ship piloted. */
    void SitIn(APilotSeat* NewSeat);

    /** Leave the seat, returning to the seat's exit point. */
    void StandUp();

    bool IsSeated() const { return Seat != nullptr; }

protected:
    virtual void BeginPlay() override;

    /**
     * Holds the camera at the head bone. A zero-length spring arm rather than
     * attaching the camera directly, for its lag: the camera follows the
     * head's position through running bob, crouching and sitting, smoothed so
     * the bob is not nauseating. Rotation stays under the mouse. Tune
     * CameraLagSpeed and SocketOffset in the Blueprint.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraArm;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    /** Bone the camera rides, hidden from the player's own view. */
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FName HeadBone = TEXT("head");

    /** How far the player can reach, in centimetres. */
    UPROPERTY(EditDefaultsOnly, Category = "Interaction")
    float InteractionRange = 250.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    /**
     * Mouse look ships as a second context in the First Person template:
     * IMC_Default binds IA_Look to Gamepad_Right2D only, and Mouse2D lives in
     * IMC_MouseLook driving its own action. Both must be added or the mouse
     * does nothing while a gamepad works fine.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> MouseLookMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    /** Mouse equivalent of LookAction; both drive the same handler. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> MouseLookAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> InteractAction;

    /** Held to sprint. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> SprintAction;

    /** Pressed to toggle crouch. */
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> CrouchAction;

    /** How far the view may turn from the seat's facing while seated, degrees. */
    UPROPERTY(EditDefaultsOnly, Category = "Seat")
    float SeatedYawLimit = 100.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Seat")
    float SeatedPitchLimit = 70.0f;

    /**
     * Widget shown for the whole session; it reads GetCurrentPrompt() itself
     * rather than being pushed text, so there is one source of truth.
     */
    UPROPERTY(EditDefaultsOnly, Category = "UI")
    TSubclassOf<UUserWidget> HUDWidgetClass;

private:
    void Move(const FInputActionValue& Value);
    void StopMoving(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void TryInteract();
    void StartSprinting();
    void StopSprinting();
    void ToggleCrouch();

    /** Sets MaxWalkSpeed from the sprint request and the movement rules. */
    void UpdateWalkSpeed();

    /** Restricts or restores how far the camera may turn. */
    void SetViewLimits(bool bSeated, float SeatYaw);

    /** Re-runs the reach trace and updates FocusedInteractable. */
    void UpdateFocusedInteractable();

    UPROPERTY()
    TObjectPtr<UInteractableComponent> FocusedInteractable;

    UPROPERTY()
    TObjectPtr<UUserWidget> HUDWidget;

    /** The seat we are sitting in, or null. */
    UPROPERTY()
    TObjectPtr<APilotSeat> Seat;

    /** IA_Move's latest value, kept for the sprint rule; zero when released. */
    FVector2D MoveInput = FVector2D::ZeroVector;

    bool bWantsToSprint = false;
};
```

Replace the whole of `Source/DeepSpace/Player/DeepSpaceCharacter.cpp`:

```cpp
#include "Player/DeepSpaceCharacter.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Blueprint/UserWidget.h"
#include "Player/MovementRules.h"
#include "Ship/InteractableComponent.h"
#include "Ship/PilotSeat.h"
#include "Ship/ShipSubsystem.h"

ADeepSpaceCharacter::ADeepSpaceCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // The mannequin's root is at its feet and it faces +Y; the capsule's
    // origin is at its centre and the pawn faces +X.
    GetMesh()->SetRelativeLocationAndRotation(
        FVector(0.0f, 0.0f, -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()),
        FRotator(0.0f, -90.0f, 0.0f));

    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(GetMesh(), HeadBone);
    CameraArm->TargetArmLength = 0.0f;
    CameraArm->bDoCollisionTest = false;
    CameraArm->bUsePawnControlRotation = true;
    CameraArm->bEnableCameraLag = true;
    CameraArm->CameraLagSpeed = 15.0f;
    // Just forward of the head bone, so the view is not from inside the skull.
    CameraArm->SocketOffset = FVector(8.0f, 0.0f, 0.0f);

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
    FirstPersonCamera->bUsePawnControlRotation = false;

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->MaxWalkSpeed = FMovementRules::WalkSpeed;
    Movement->MaxWalkSpeedCrouched = FMovementRules::CrouchSpeed;
    Movement->NavAgentProps.bCanCrouch = true;
    Movement->SetCrouchedHalfHeight(FMovementRules::CrouchedHalfHeight);
}

void ADeepSpaceCharacter::BeginPlay()
{
    Super::BeginPlay();

    // The camera sits at the head; hide it so the view is not from inside it.
    // Known stand-in: this also removes the head from the player's shadow.
    GetMesh()->HideBoneByName(HeadBone, EPhysBodyOp::PBO_None);

    APlayerController* PC = Cast<APlayerController>(GetController());
    if (PC)
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            for (UInputMappingContext* Context : {DefaultMappingContext.Get(),
                                                  MouseLookMappingContext.Get()})
            {
                if (Context)
                {
                    Subsystem->AddMappingContext(Context, 0);
                }
            }
        }

        if (HUDWidgetClass)
        {
            HUDWidget = CreateWidget<UUserWidget>(PC, HUDWidgetClass);
            if (HUDWidget)
            {
                HUDWidget->AddToViewport();
            }
        }
    }
}

void ADeepSpaceCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateWalkSpeed();
    UpdateFocusedInteractable();
}

void ADeepSpaceCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (!Input)
    {
        return;
    }

    if (MoveAction)
    {
        Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Move);
        Input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::StopMoving);
    }
    if (LookAction)
    {
        Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Look);
    }
    if (MouseLookAction)
    {
        Input->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Look);
    }
    if (JumpAction)
    {
        Input->BindAction(JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
        Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
    }
    if (InteractAction)
    {
        Input->BindAction(InteractAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::TryInteract);
    }
    if (SprintAction)
    {
        Input->BindAction(SprintAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::StartSprinting);
        Input->BindAction(SprintAction, ETriggerEvent::Completed, this, &ADeepSpaceCharacter::StopSprinting);
    }
    if (CrouchAction)
    {
        Input->BindAction(CrouchAction, ETriggerEvent::Started, this, &ADeepSpaceCharacter::ToggleCrouch);
    }
}

void ADeepSpaceCharacter::Move(const FInputActionValue& Value)
{
    MoveInput = Value.Get<FVector2D>();
    if (!Controller || IsSeated())
    {
        return;
    }
    AddMovementInput(GetActorForwardVector(), MoveInput.Y);
    AddMovementInput(GetActorRightVector(), MoveInput.X);
}

void ADeepSpaceCharacter::StopMoving(const FInputActionValue& Value)
{
    MoveInput = FVector2D::ZeroVector;
}

void ADeepSpaceCharacter::StartSprinting()
{
    bWantsToSprint = true;
}

void ADeepSpaceCharacter::StopSprinting()
{
    bWantsToSprint = false;
}

void ADeepSpaceCharacter::ToggleCrouch()
{
    if (IsSeated())
    {
        return;
    }
    // UnCrouch is refused by the movement component while something is
    // overhead -- inside the crawlway, pressing C again does nothing.
    if (bIsCrouched)
    {
        UnCrouch();
    }
    else
    {
        Crouch();
    }
}

void ADeepSpaceCharacter::UpdateWalkSpeed()
{
    const bool bSprinting = bWantsToSprint && FMovementRules::CanSprint(bIsCrouched, MoveInput);
    GetCharacterMovement()->MaxWalkSpeed =
        bSprinting ? FMovementRules::SprintSpeed : FMovementRules::WalkSpeed;
}

EPosture ADeepSpaceCharacter::GetPosture() const
{
    if (IsSeated())
    {
        return EPosture::Seated;
    }
    if (GetCharacterMovement()->IsFalling())
    {
        return EPosture::Falling;
    }
    return bIsCrouched ? EPosture::Crouched : EPosture::Standing;
}

void ADeepSpaceCharacter::SitIn(APilotSeat* NewSeat)
{
    if (!NewSeat || IsSeated())
    {
        return;
    }
    if (bIsCrouched)
    {
        UnCrouch();
    }

    Seat = NewSeat;
    FocusedInteractable = nullptr;

    // Off: movement, and collision, so the capsule may overlap the chair.
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorEnableCollision(false);

    // The seat anchor is on the floor; the actor's origin is the capsule's
    // centre. The body faces the seat's way, and stays that way while the
    // head turns: it no longer follows the controller's yaw.
    const FTransform SeatTransform = Seat->GetSeatTransform();
    const float SeatYaw = SeatTransform.Rotator().Yaw;
    SetActorLocationAndRotation(
        SeatTransform.GetLocation() + FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),
        FRotator(0.0f, SeatYaw, 0.0f));
    bUseControllerRotationYaw = false;
    if (Controller)
    {
        Controller->SetControlRotation(FRotator(0.0f, SeatYaw, 0.0f));
    }
    SetViewLimits(true, SeatYaw);

    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->SetPilot(this);
    }
}

void ADeepSpaceCharacter::StandUp()
{
    if (!IsSeated())
    {
        return;
    }
    const FVector Exit = Seat->GetExitLocation();
    Seat = nullptr;

    SetActorLocation(Exit + FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    SetActorEnableCollision(true);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    bUseControllerRotationYaw = true;
    SetViewLimits(false, 0.0f);

    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->ClearPilot();
    }
}

void ADeepSpaceCharacter::SetViewLimits(bool bSeated, float SeatYaw)
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager : nullptr;
    if (!Camera)
    {
        return;
    }
    if (bSeated)
    {
        Camera->ViewYawMin = SeatYaw - SeatedYawLimit;
        Camera->ViewYawMax = SeatYaw + SeatedYawLimit;
        Camera->ViewPitchMin = -SeatedPitchLimit;
        Camera->ViewPitchMax = SeatedPitchLimit;
    }
    else
    {
        // The engine's defaults: free yaw, pitch just short of straight up/down.
        const APlayerCameraManager* Defaults = GetDefault<APlayerCameraManager>();
        Camera->ViewYawMin = Defaults->ViewYawMin;
        Camera->ViewYawMax = Defaults->ViewYawMax;
        Camera->ViewPitchMin = Defaults->ViewPitchMin;
        Camera->ViewPitchMax = Defaults->ViewPitchMax;
    }
}

void ADeepSpaceCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    AddControllerYawInput(Axis.X);
    AddControllerPitchInput(Axis.Y);
}

void ADeepSpaceCharacter::UpdateFocusedInteractable()
{
    FocusedInteractable = nullptr;

    // Seated, E means "stand up" whatever the player is looking at.
    if (!FirstPersonCamera || IsSeated())
    {
        return;
    }

    const FVector Start = FirstPersonCamera->GetComponentLocation();
    const FVector End = Start + FirstPersonCamera->GetForwardVector() * InteractionRange;

    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
    {
        return;
    }

    if (AActor* HitActor = Hit.GetActor())
    {
        UInteractableComponent* Interactable = HitActor->FindComponentByClass<UInteractableComponent>();
        if (Interactable && Interactable->CanInteract())
        {
            FocusedInteractable = Interactable;
        }
    }
}

void ADeepSpaceCharacter::TryInteract()
{
    if (IsSeated())
    {
        StandUp();
        return;
    }
    if (FocusedInteractable)
    {
        FocusedInteractable->Interact(this);
    }
}

UInteractableComponent* ADeepSpaceCharacter::GetFocusedInteractable() const
{
    return FocusedInteractable;
}

FText ADeepSpaceCharacter::GetCurrentPrompt() const
{
    if (IsSeated())
    {
        return NSLOCTEXT("DeepSpace", "StandUp", "Stand up");
    }
    return FocusedInteractable ? FocusedInteractable->GetPrompt() : FText::GetEmpty();
}
```

Two details worth understanding:
- **`FirstPersonCamera` keeps its name** but is now attached to `CameraArm`, which is attached to the mesh's `head` bone. `BP_DeepSpaceCharacter` inherits native components by name, so keeping it avoids breaking the Blueprint.
- **Seated, the body stops following the controller's yaw** (`bUseControllerRotationYaw = false`), so looking around turns the head, not the chair; standing restores it.

- [ ] **Step 5: Build, run all tests, and confirm the contract test bites**

```bash
./rebuild.sh 2>&1 | grep -E "error|Result:"
rm -f Saved/Logs/DeepSpace.log
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace" -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding >/dev/null 2>&1
grep -E "Test Completed|standing .*crouched" Saved/Logs/DeepSpace.log
```

Expected: four `Result={Success}` (`MovementContract`, `Sprint`, `Piloted`, `PowerState`) and `BP_DeepSpaceCharacter: standing 176, crouched 130, radius 34; ship: stand 180, crouch 140, radius 34`.

Then tighten the contract without rebuilding, and restore:

```bash
cp Tools/movement_contract.json /tmp/claude-1000/mc.bak
sed -i 's/"crouch_clearance": 140/"crouch_clearance": 120/' Tools/movement_contract.json
rm -f Saved/Logs/DeepSpace.log
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace.Player.MovementContract" -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding >/dev/null 2>&1
grep -E "Test Completed|Expected" Saved/Logs/DeepSpace.log
cp /tmp/claude-1000/mc.bak Tools/movement_contract.json
```

Expected: `Result={Fail}` with `Expected 'crouched capsule fits under the crouch clearance' to be true.`

- [ ] **Step 6: Commit**

```bash
git add Source/DeepSpace/
git commit -m "feat: sprint, crouch, a body the camera rides, and the pilot seat"
```

---

### Task 5: Import and retarget the Mixamo animations; build the blend spaces

**Close the editor before building.**

**Files:**
- Create: `Source/DeepSpace/Editor/DeepSpaceEditorScripting.h/.cpp`, `Tools/import_animations.py`
- Generated: `Content/Characters/DeepSpace/{Mixamo,Rigs,Anims}/…`

**Interfaces:**
- Produces: `unreal.DeepSpaceEditorScripting.rebuild_blend_space(bs) -> bool`. Assets: `/Game/Characters/DeepSpace/Anims/RTG_<clip>` for every FBX in `SourceArt/Mixamo/` except `y_bot.fbx`; `/Game/Characters/DeepSpace/Anims/BS_DS_Locomotion` (Speed 0–600: `MM_Idle`@0, `MF_Unarmed_Walk_Fwd`@300, `RTG_running`@600) and `BS_DS_Crouch` (Speed 0–150: `RTG_crouching_idle`@0, `RTG_crouch_walk`@150).

- [ ] **Step 1: Add the blend-space helper**

`Source/DeepSpace/Editor/DeepSpaceEditorScripting.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DeepSpaceEditorScripting.generated.h"

class UBlendSpace;

/**
 * Engine operations the editor-side Python scripts under Tools/ need but that
 * Unreal does not expose to Python. Editor-only: every function does nothing
 * and returns false in a cooked game.
 */
UCLASS()
class DEEPSPACE_API UDeepSpaceEditorScripting : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /**
     * Rebuild a blend space's runtime interpolation data from its samples.
     *
     * Setting a blend space's samples and axes from Python stores them but
     * never builds the table the engine blends from -- that happens only in
     * the blend space editor's change notifications. A blend space built from
     * a script without this saves and loads cleanly and then blends nothing.
     */
    UFUNCTION(BlueprintCallable, Category = "DeepSpace|Editor")
    static bool RebuildBlendSpace(UBlendSpace* BlendSpace);
};
```

`Source/DeepSpace/Editor/DeepSpaceEditorScripting.cpp`:

```cpp
#include "Editor/DeepSpaceEditorScripting.h"

#include "Animation/BlendSpace.h"

bool UDeepSpaceEditorScripting::RebuildBlendSpace(UBlendSpace* BlendSpace)
{
#if WITH_EDITOR
    if (!BlendSpace)
    {
        return false;
    }
    BlendSpace->ValidateSampleData();
    BlendSpace->ResampleData();
    BlendSpace->MarkPackageDirty();
    return true;
#else
    return false;
#endif
}
```

Run: `./rebuild.sh 2>&1 | grep -E "error|Result:"` — expected `Result: Succeeded`.

- [ ] **Step 2: Write the importer**

`Tools/import_animations.py`:

```python
"""
Imports the Mixamo clips in SourceArt/Mixamo/, retargets them onto the
mannequin, and builds the body's blend spaces.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/import_animations.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/import_animations.txt

Mixamo clips are on Mixamo's own skeleton. Retargeting maps them onto Unreal's
mannequin through two IK rigs and a retargeter, all generated here: the IK Rig
plugin recognises both skeletons, so their retarget chains and the pose
alignment (Mixamo is T-pose, the mannequin A-pose) are automatic.

Idempotent, and deliberately *updates in place*: every asset keeps its path
across runs, because ABP_DeepSpaceBody is wired by hand and references the
blend spaces. Deleting and recreating them would silently break it.

Every .fbx in SourceArt/Mixamo/ is imported, so a new download is picked up by
re-running; wiring it into a blend space means editing BLEND_SPACES below.
"""

import glob
import os
import traceback

import unreal

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIR = os.path.join(PROJECT, "SourceArt", "Mixamo")
SOURCE_SKELETON_FBX = "y_bot.fbx"

ROOT = "/Game/Characters/DeepSpace"
IMPORT_DIR = ROOT + "/Mixamo"          # raw imports, on Mixamo's skeleton
RIG_DIR = ROOT + "/Rigs"
ANIM_DIR = ROOT + "/Anims"             # retargeted, on the mannequin
PREFIX = "RTG_"

MANNY = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"
UNARMED = "/Game/Characters/Mannequins/Anims/Unarmed"

# name -> (axis label, axis max, [(animation path, axis value)])
# Axis values are ground speeds in cm/s, matching FMovementRules.
BLEND_SPACES = {
    "BS_DS_Locomotion": ("Speed", 600.0, [
        (UNARMED + "/MM_Idle", 0.0),
        (UNARMED + "/Walk/MF_Unarmed_Walk_Fwd", 300.0),
        (ANIM_DIR + "/" + PREFIX + "running", 600.0),
    ]),
    "BS_DS_Crouch": ("Speed", 150.0, [
        (ANIM_DIR + "/" + PREFIX + "crouching_idle", 0.0),
        (ANIM_DIR + "/" + PREFIX + "crouch_walk", 150.0),
    ]),
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
LOG = []


def log(*parts):
    LOG.append(" ".join(str(p) for p in parts))


def load_or_create(name, folder, cls, factory):
    path = "%s/%s" % (folder, name)
    if lib.does_asset_exist(path):
        return unreal.load_asset(path)
    return tools.create_asset(name, folder, cls, factory)


def import_fbx(filename, destination, skeleton=None):
    """Import one FBX, replacing any previous import at the same path."""
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = destination
    task.automated = True
    task.save = True
    task.replace_existing = True
    options = unreal.FbxImportUI()
    options.import_materials = False
    options.import_textures = False
    options.import_as_skeletal = True
    if skeleton is None:
        options.import_mesh = True
        options.import_animations = False
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    else:
        options.import_mesh = False
        options.import_animations = True
        options.skeleton = skeleton
        options.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    task.options = options
    tools.import_asset_tasks([task])
    return [unreal.load_asset(p) for p in task.imported_object_paths]


def import_sources():
    """Y Bot for its skeleton, then every clip onto that skeleton."""
    imported = import_fbx(os.path.join(SOURCE_DIR, SOURCE_SKELETON_FBX), IMPORT_DIR)
    y_bot = next((a for a in imported if isinstance(a, unreal.SkeletalMesh)), None)
    if y_bot is None:
        raise RuntimeError("%s produced no skeletal mesh" % SOURCE_SKELETON_FBX)

    clips = []
    for path in sorted(glob.glob(os.path.join(SOURCE_DIR, "*.fbx"))):
        if os.path.basename(path) == SOURCE_SKELETON_FBX:
            continue
        name = os.path.splitext(os.path.basename(path))[0]
        seqs = [a for a in import_fbx(path, IMPORT_DIR + "/" + name, y_bot.skeleton)
                if isinstance(a, unreal.AnimSequence)]
        if not seqs:
            raise RuntimeError("%s produced no animation" % os.path.basename(path))
        clips += seqs
        log("imported", name, "%.2fs" % seqs[0].get_play_length())
    return y_bot, clips


def ik_rig(name, mesh):
    """An IK rig whose retarget chains are generated from the skeleton."""
    rig = load_or_create(name, RIG_DIR, unreal.IKRigDefinition, unreal.IKRigDefinitionFactory())
    controller = unreal.IKRigController.get_controller(rig)
    controller.set_skeletal_mesh(mesh)
    if not controller.apply_auto_generated_retarget_definition():
        raise RuntimeError("could not auto-generate retarget chains for " + name)
    controller.apply_auto_fbik()
    lib.save_loaded_asset(rig, only_if_is_dirty=False)
    log("rig", name, "root", controller.get_retarget_root(),
        "chains", len(controller.get_retarget_chains()))
    return rig


def retargeter(source_rig, target_rig, source_mesh, target_mesh):
    rtg = load_or_create("RTG_Mixamo_To_Manny", RIG_DIR, unreal.IKRetargeter,
                         unreal.IKRetargetFactory())
    c = unreal.IKRetargeterController.get_controller(rtg)
    source, target = unreal.RetargetSourceOrTarget.SOURCE, unreal.RetargetSourceOrTarget.TARGET
    c.set_ik_rig(source, source_rig)
    c.set_ik_rig(target, target_rig)
    c.set_preview_mesh(source, source_mesh)
    c.set_preview_mesh(target, target_mesh)
    if c.get_num_retarget_ops() == 0:
        c.add_default_ops()
    c.assign_ik_rig_to_all_ops(source, source_rig)
    c.assign_ik_rig_to_all_ops(target, target_rig)
    c.auto_map_chains(unreal.AutoMapChainType.FUZZY, True)
    # Mixamo's rest pose is a T-pose, the mannequin's an A-pose.
    c.auto_align_all_bones(target, unreal.RetargetAutoAlignMethod.CHAIN_TO_CHAIN)
    lib.save_loaded_asset(rtg, only_if_is_dirty=False)
    return rtg


def retarget(clips, rtg, source_mesh, target_mesh):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    inputs = unreal.IKRetargetBatchOperationInputs()
    inputs.set_editor_property("assets_to_retarget",
                               [registry.get_asset_by_object_path(c.get_path_name()) for c in clips])
    inputs.set_editor_property("source_mesh", source_mesh)
    inputs.set_editor_property("target_mesh", target_mesh)
    inputs.set_editor_property("ik_retarget_asset", rtg)
    inputs.set_editor_property("target_path", ANIM_DIR)
    inputs.set_editor_property("prefix", PREFIX)
    inputs.set_editor_property("include_referenced_assets", False)
    inputs.set_editor_property("overwrite_existing_files", True)
    for data in unreal.IKRetargetBatchOperation.run_batch_retarget(inputs):
        asset = unreal.load_asset("%s.%s" % (data.package_name, data.asset_name))
        if isinstance(asset, unreal.AnimSequence):
            lib.save_loaded_asset(asset, only_if_is_dirty=False)
            log("retargeted", asset.get_name())


def blend_space(name, label, maximum, samples, skeleton):
    factory = unreal.BlendSpaceFactory1D()
    factory.set_editor_property("target_skeleton", skeleton)
    bs = load_or_create(name, ANIM_DIR, unreal.BlendSpace1D, factory)

    params = list(bs.get_editor_property("blend_parameters"))
    axis = params[0]
    axis.set_editor_property("display_name", label)
    axis.set_editor_property("min", 0.0)
    axis.set_editor_property("max", maximum)
    axis.set_editor_property("grid_num", 4)
    params[0] = axis
    bs.set_editor_property("blend_parameters", params)

    entries = []
    for path, value in samples:
        anim = unreal.load_asset(path)
        if anim is None:
            raise RuntimeError("%s: missing sample %s" % (name, path))
        sample = unreal.BlendSample()
        sample.set_editor_property("animation", anim)
        sample.set_editor_property("sample_value", unreal.Vector(value, 0.0, 0.0))
        entries.append(sample)
    bs.set_editor_property("sample_data", entries)

    # Without this the samples are stored but the interpolation table the
    # engine blends from is never built: see UDeepSpaceEditorScripting.
    if not unreal.DeepSpaceEditorScripting.rebuild_blend_space(bs):
        raise RuntimeError("could not rebuild " + name)
    lib.save_loaded_asset(bs, only_if_is_dirty=False)
    log("blend space", name, ", ".join("%s@%g" % (p.rsplit("/", 1)[-1], v) for p, v in samples))


def main():
    try:
        manny = unreal.load_asset(MANNY)
        y_bot, clips = import_sources()
        source_rig = ik_rig("IK_Mixamo", y_bot)
        target_rig = ik_rig("IK_Manny", manny)
        rtg = retargeter(source_rig, target_rig, y_bot, manny)
        retarget(clips, rtg, y_bot, manny)
        for name, (label, maximum, samples) in BLEND_SPACES.items():
            blend_space(name, label, maximum, samples, manny.skeleton)
        log("DONE")
    except Exception:
        log("FAILED\n" + traceback.format_exc())
    with open(os.path.join(unreal.Paths.project_saved_dir(), "import_animations.txt"), "w") as f:
        f.write("\n".join(LOG) + "\n")


main()
```

- [ ] **Step 3: Run it**

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/import_animations.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
cat Saved/import_animations.txt
```

Expected: ten `imported` lines (crouch_walk 3.00s, crouching_idle 2.50s, pilot_flips_switches_a 5.03s, pilot_flips_switches_b 5.00s, running 0.70s, seated_idle 4.17s, sit_to_stand 2.27s, sitting_idle 4.30s, stand_to_sit 2.23s, typing 16.47s); `rig IK_Mixamo root Hips chains 21`; `rig IK_Manny root pelvis chains 29`; ten `retargeted RTG_…`; two `blend space` lines; `DONE`.

- [ ] **Step 4: Confirm the blend spaces are processed, and re-running is safe**

A blend space built without the helper saves and loads cleanly but blends nothing. The processed data is visible in the asset:

```bash
find Content/Characters/DeepSpace -name "*.uasset" | sort > /tmp/claude-1000/run1.txt
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/import_animations.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
tail -1 Saved/import_animations.txt
find Content/Characters/DeepSpace -name "*.uasset" | sort | diff /tmp/claude-1000/run1.txt - && echo "same paths"
for b in BS_DS_Locomotion BS_DS_Crouch; do
  echo "$b: $(strings -n 5 Content/Characters/DeepSpace/Anims/$b.uasset | grep -cE '^(BlendSpaceData|Segments)$') of 2"
done
```

Expected: `DONE`; `same paths` (27 assets); `2 of 2` for both.

- [ ] **Step 5: Commit**

```bash
git add Source/DeepSpace/Editor Tools/import_animations.py Content/Characters/DeepSpace
git commit -m "feat: import and retarget Mixamo clips; build the body's blend spaces"
```

---

### Task 6: Scripted character setup, and the two animation guards

**Files:**
- Create: `Tools/setup_character.py`, `Tools/check_anim_heights.py`, `Tools/check_anim_blueprints.py`
- Generated: `Content/Input/Actions/IA_Sprint.uasset`, `IA_Crouch.uasset`, `Content/Characters/DeepSpace/ABP_DeepSpaceBody.uasset`
- Modified by script: `Content/Input/IMC_Default.uasset`, `Content/Blueprints/BP_DeepSpaceCharacter.uasset`

**Interfaces:**
- Consumes: `sprint_action`, `crouch_action`, `unreal.DeepSpaceAnimInstance` (Task 4); retargeted clips (Task 5).
- Produces: `ABP_DeepSpaceBody` shell (parent `UDeepSpaceAnimInstance`, skeleton `SK_Mannequin`, graph not yet wired); `BP_DeepSpaceCharacter` with `SprintAction`, `CrouchAction`, mesh `SKM_Manny_Simple`, anim class `ABP_DeepSpaceBody_C`.

- [ ] **Step 1: Write and run the setup script**

`Tools/setup_character.py`:

```python
"""
Sets up the player's input actions and Blueprint defaults for movement and
the body.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/setup_character.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/setup_character.txt

Does everything around the character that is asset *assignment* -- the kind
of Blueprint work the project's rules allow -- so it is reviewable here rather
than clicked in the editor:

  * IA_Sprint and IA_Crouch, bound to Left Shift and C in IMC_Default;
  * the ABP_DeepSpaceBody shell, parented to UDeepSpaceAnimInstance on the
    mannequin skeleton (its six-node graph is wired by hand: Python cannot
    reasonably build anim graphs);
  * BP_DeepSpaceCharacter's defaults: those two actions, the mannequin as its
    body, and the Animation Blueprint as its anim class.

Idempotent. Run import_animations.py first: the mesh and skeleton come from
the template, but the Animation Blueprint is only useful once the clips exist.
"""

import os
import traceback

import unreal

ACTIONS = "/Game/Input/Actions"
MAPPING_CONTEXT = "/Game/Input/IMC_Default"
CHARACTER_BP = "/Game/Blueprints/BP_DeepSpaceCharacter"
BODY_ABP_DIR = "/Game/Characters/DeepSpace"
BODY_ABP = "ABP_DeepSpaceBody"
BODY_MESH = "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"

# action name -> key, both digital
BINDINGS = {"IA_Sprint": "LeftShift", "IA_Crouch": "C"}

tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
LOG = []


def log(*parts):
    LOG.append(" ".join(str(p) for p in parts))


def input_action(name):
    path = "%s/%s" % (ACTIONS, name)
    action = unreal.load_asset(path) if lib.does_asset_exist(path) else \
        tools.create_asset(name, ACTIONS, unreal.InputAction, unreal.InputAction_Factory())
    action.set_editor_property("value_type", unreal.InputActionValueType.BOOLEAN)
    lib.save_loaded_asset(action, only_if_is_dirty=False)
    return action


def bind(context, action, key_name):
    # Unmap first, so re-running never stacks duplicate bindings.
    context.unmap_all_keys_from_action(action)
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    context.map_key(action, key)
    log("bound", action.get_name(), "->", key_name)


def body_anim_blueprint(skeleton):
    path = "%s/%s" % (BODY_ABP_DIR, BODY_ABP)
    if lib.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("parent_class", unreal.DeepSpaceAnimInstance)
    factory.set_editor_property("target_skeleton", skeleton)
    abp = tools.create_asset(BODY_ABP, BODY_ABP_DIR, unreal.AnimBlueprint, factory)
    lib.save_loaded_asset(abp, only_if_is_dirty=False)
    log("created", path, "(graph to be wired by hand)")
    return abp


def main():
    try:
        context = unreal.load_asset(MAPPING_CONTEXT)
        actions = {}
        for name, key in BINDINGS.items():
            actions[name] = input_action(name)
            bind(context, actions[name], key)
        lib.save_loaded_asset(context, only_if_is_dirty=False)

        mesh = unreal.load_asset(BODY_MESH)
        abp = body_anim_blueprint(mesh.skeleton)

        bp = unreal.load_asset(CHARACTER_BP)
        defaults = unreal.get_default_object(bp.generated_class())
        defaults.set_editor_property("sprint_action", actions["IA_Sprint"])
        defaults.set_editor_property("crouch_action", actions["IA_Crouch"])
        body = defaults.get_editor_property("mesh")
        body.set_skeletal_mesh_asset(mesh)
        body.set_editor_property("anim_class", abp.generated_class())
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        lib.save_loaded_asset(bp, only_if_is_dirty=False)
        log("BP_DeepSpaceCharacter: sprint, crouch, body mesh and anim class assigned")
        log("DONE")
    except Exception:
        log("FAILED\n" + traceback.format_exc())
    with open(os.path.join(unreal.Paths.project_saved_dir(), "setup_character.txt"), "w") as f:
        f.write("\n".join(LOG) + "\n")


main()
```

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_character.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
cat Saved/setup_character.txt
strings -n 1 Content/Input/IMC_Default.uasset | grep -E "^(LeftShift|C)$|IA_Sprint|IA_Crouch" | sort -u
```

Expected: `bound IA_Sprint -> LeftShift`, `bound IA_Crouch -> C`, `created …ABP_DeepSpaceBody`, `BP_DeepSpaceCharacter: sprint, crouch, body mesh and anim class assigned`, `DONE`; the grep shows both actions and both keys.

- [ ] **Step 2: Write and run the anim-height guard**

`Tools/check_anim_heights.py`:

```python
"""
Checks the camera stays inside the capsule in every animation.

    ~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \\
        "$PWD/DeepSpace.uproject" \\
        -run=pythonscript -script="$PWD/Tools/check_anim_heights.py" \\
        -unattended -nopause -nosplash -NoLiveCoding
    cat Saved/check_anim_heights.txt

The camera rides the body's head bone. The engine guarantees the capsule fits
wherever the player stands, so a head that never rises above the capsule's top
can never carry the camera through a ceiling -- in the crawlway or anywhere.
This samples each clip and checks the head's peak against the capsule for the
posture that plays it, read from BP_DeepSpaceCharacter itself.

It exists because the first crouch-walk clip peaked at 118 cm, above the
original 110 cm crawlway: the view would have passed through the ceiling on
every step. A future, taller clip fails here instead.
"""

import os
import traceback

import unreal

CHARACTER_BP = "/Game/Blueprints/BP_DeepSpaceCharacter"
UNARMED = "/Game/Characters/Mannequins/Anims/Unarmed"
ANIMS = "/Game/Characters/DeepSpace/Anims"
HEAD = "head"
SAMPLES = 40

# Room between the head bone and the capsule top: the camera's near plane
# and the lag of the spring arm holding it.
MARGIN = 5.0

POSTURE_CLIPS = {
    "standing": [UNARMED + "/MM_Idle", UNARMED + "/Walk/MF_Unarmed_Walk_Fwd", ANIMS + "/RTG_running"],
    "crouched": [ANIMS + "/RTG_crouching_idle", ANIMS + "/RTG_crouch_walk"],
    # Getting in and out of the seat starts and ends standing.
    "seat transitions": [ANIMS + "/RTG_stand_to_sit", ANIMS + "/RTG_sit_to_stand"],
}


def peak_head(anim):
    options = unreal.AnimPoseEvaluationOptions()
    length = anim.get_play_length()
    peak = -1e9
    for i in range(SAMPLES + 1):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(anim, length * i / SAMPLES, options)
        z = unreal.AnimPoseExtensions.get_bone_pose(pose, HEAD, unreal.AnimPoseSpaces.WORLD).translation.z
        peak = max(peak, z)
    return peak


def main():
    lines, failed = [], False
    try:
        bp = unreal.load_asset(CHARACTER_BP)
        defaults = unreal.get_default_object(bp.generated_class())
        standing = 2.0 * defaults.get_editor_property("capsule_component").get_unscaled_capsule_half_height()
        crouched = 2.0 * defaults.get_editor_property("character_movement").get_editor_property("crouched_half_height")
        limits = {"standing": standing, "crouched": crouched, "seat transitions": standing}
        lines.append("capsule: standing %.0f cm, crouched %.0f cm; margin %.0f cm" % (standing, crouched, MARGIN))

        for posture, clips in POSTURE_CLIPS.items():
            limit = limits[posture] - MARGIN
            for path in clips:
                anim = unreal.load_asset(path)
                if anim is None:
                    lines.append("FAIL %-16s %s is missing" % (posture, path))
                    failed = True
                    continue
                peak = peak_head(anim)
                ok = peak <= limit
                failed |= not ok
                lines.append("%s %-16s %-22s head peaks %.1f cm, limit %.1f" %
                             ("ok  " if ok else "FAIL", posture, anim.get_name(), peak, limit))
    except Exception:
        failed = True
        lines.append("FAIL\n" + traceback.format_exc())
    lines.append("")
    lines.append("FAIL: a head rises above its capsule; the camera can leave it." if failed
                 else "PASS: every clip keeps the head, and the camera, inside the capsule.")
    with open(os.path.join(unreal.Paths.project_saved_dir(), "check_anim_heights.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")


main()
```

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_anim_heights.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
cat Saved/check_anim_heights.txt
```

Expected: `capsule: standing 176 cm, crouched 130 cm; margin 5 cm`, seven `ok` lines (crouch walk *head peaks 118.0 cm, limit 125.0*), `PASS`.

Confirm it bites: temporarily set `MARGIN = 20.0`, re-run, expect `FAIL crouched RTG_crouch_walk head peaks 118.0 cm, limit 110.0` among others; restore `MARGIN = 5.0`.

- [ ] **Step 3: Write and run the wiring-only guard**

`Tools/check_anim_blueprints.py`:

```python
#!/usr/bin/env python3
"""
Checks Animation Blueprints hold wiring, not logic.

    python3 Tools/check_anim_blueprints.py

The project's rule is that all gameplay logic lives in C++ (ADR 0002). An
Animation Blueprint is a node graph in a binary asset, which is exactly where
logic goes to become invisible. ABP_DeepSpaceBody is allowed to *wire* values
C++ computed -- read a variable, play a blend space or a sequence, choose a
pose by an enum, output it -- and nothing else.

This is an allowlist, not a denylist: a node type nobody anticipated fails
until someone decides it belongs here. No editor needed: node class names are
stored as plain text in the asset, which is read as bytes. Exits non-zero on
any violation, so it can gate a build.
"""

import glob
import os
import re
import sys

PROJECT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Every Animation Blueprint the project owns. The template's are Epic's, and
# full of state machines; they are not ours to police.
ANIM_BLUEPRINTS = ["Content/Characters/DeepSpace/ABP_*.uasset"]

ALLOWED = {
    "AnimGraphNode_Root",                 # the output pose
    "AnimGraphNode_BlendSpacePlayer",
    "AnimGraphNode_SequencePlayer",
    "AnimGraphNode_BlendListByEnum",      # "Blend Poses by EPosture"
    "AnimGraphNode_BlendListByBool",
    "AnimGraphNode_BlendListByInt",
    "K2Node_Event",                       # default event stubs every ABP gets
    "K2Node_VariableGet",                 # reading a value C++ computed
}

NODE = re.compile(rb"(?:AnimGraphNode|K2Node)_[A-Za-z0-9]+(?:_[A-Za-z0-9]+)*")


def node_tokens(path):
    with open(path, "rb") as f:
        return {m.decode() for m in NODE.findall(f.read())}


def allowed(token):
    # Tokens are class names, or instance and pin names derived from them
    # ("K2Node_Event_0", "AnimGraphNode_Root_0").
    return any(token == a or token.startswith(a + "_") for a in ALLOWED)


def main():
    files = sorted(f for pattern in ANIM_BLUEPRINTS
                   for f in glob.glob(os.path.join(PROJECT, pattern)))
    if not files:
        print("FAIL: no Animation Blueprints found to check (%s)" % ", ".join(ANIM_BLUEPRINTS))
        return 1
    failed = False
    for path in files:
        bad = sorted(t for t in node_tokens(path) if not allowed(t))
        name = os.path.relpath(path, PROJECT)
        if bad:
            failed = True
            print("FAIL %s contains logic, not wiring:" % name)
            for t in bad:
                print("  - " + t)
        else:
            print("ok   %s" % name)
    if failed:
        print("\nAnimation Blueprints may only wire values computed in C++ (ADR 0002).")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
```

```bash
chmod +x Tools/check_anim_blueprints.py
python3 Tools/check_anim_blueprints.py; echo "exit $?"
python3 -c "
import sys; sys.path.insert(0,'Tools'); import check_anim_blueprints as C
C.ANIM_BLUEPRINTS=['Content/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed.uasset']
print('exit', C.main())" | tail -3
```

Expected: `ok   Content/Characters/DeepSpace/ABP_DeepSpaceBody.uasset`, `exit 0`; the negative control against the template's `ABP_Unarmed` lists state-machine and logic nodes and ends `exit 1`.

- [ ] **Step 4: Re-run the automation tests**

As Task 2 Step 4. Expected: four `Result={Success}`; the contract line still reads `standing 176, crouched 130, radius 34`.

- [ ] **Step 5: Commit**

```bash
git add Tools/setup_character.py Tools/check_anim_heights.py Tools/check_anim_blueprints.py Content/Input Content/Characters/DeepSpace/ABP_DeepSpaceBody.uasset Content/Blueprints/BP_DeepSpaceCharacter.uasset
git commit -m "feat: scripted character setup; guards for anim heights and wiring-only anim blueprints"
```

---

### Task 7: Place the pilot seat in the level

**Files:**
- Modify: `Tools/hauler_layout.py`, `Tools/build_hauler.py`, `Tools/verify_level.py`
- Generated: `Content/Maps/L_Hauler.umap`

**Interfaces:**
- Consumes: `unreal.PilotSeat` (Task 4).
- Produces: `hauler_layout.PILOT_SEAT = ("cockpit", (175, 130), 0)`; `Ship.pilot_seat_location` = `(1585, -70, 0)`, `Ship.pilot_seat_yaw` = `0`; actor label `hauler_pilot_seat`.

- [ ] **Step 1: Name the helm in the layout**

In `Tools/hauler_layout.py`, after the `PLAYER_START = ("bunk", (200, 150), 100)` line add:

```python

# The helm: the port pilot seat. APilotSeat is placed over the decorative
# pilot_seat prop there, at floor level, facing the way that prop faces. The
# starboard seat stays decorative.
PILOT_SEAT = ("cockpit", (175, 130), 0)
```

Replace

```python
Ship = namedtuple("Ship", "plan boxes lights console_location console_yaw "
                          "player_start regions keep_clear")
```

with

```python
Ship = namedtuple("Ship", "plan boxes lights console_location console_yaw "
                          "player_start regions keep_clear "
                          "pilot_seat_location pilot_seat_yaw")
```

and replace

```python
    return Ship(plan, boxes, lights, console_location, console_yaw,
                player_start, regions, keep_clear)
```

with

```python
    seat_room, seat_at, seat_yaw = PILOT_SEAT
    pilot_seat_location = resolve_point(plan, seat_room, seat_at)

    return Ship(plan, boxes, lights, console_location, console_yaw,
                player_start, regions, keep_clear,
                pilot_seat_location, seat_yaw)
```

- [ ] **Step 2: Build it and verify it**

In `Tools/build_hauler.py`, directly after the three lines that spawn and label `hauler_player_start`, add:

```python

    # A C++ actor with no mesh of its own: it sits over the pilot_seat prop
    # and carries the interaction that puts the ship in pilot mode.
    seat = actor_sub.spawn_actor_from_class(
        unreal.PilotSeat, unreal.Vector(*ship.pilot_seat_location),
        unreal.Rotator(0, 0, ship.pilot_seat_yaw))
    seat.set_actor_label(TAG + "pilot_seat")
```

In `Tools/verify_level.py`, replace

```python
    for label, want in (("console", ship.console_location),
                        ("player_start", ship.player_start)):
```

with

```python
    for label, want in (("console", ship.console_location),
                        ("player_start", ship.player_start),
                        ("pilot_seat", ship.pilot_seat_location)):
```

- [ ] **Step 3: Validate, build, verify**

```bash
python3 Tools/validate_hauler.py | tail -1
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
cat Saved/hauler_build.txt
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/verify_level.py" \
    -unattended -nopause -nosplash -NoLiveCoding >/dev/null 2>&1
cat Saved/verify_level.txt
```

Expected: validator `PASS`; `placed 182 boxes, 31 lights, 160 stars`; verifier `PASS: the built level matches the layout within 1.0 cm.`

- [ ] **Step 4: Commit**

```bash
git add Tools/hauler_layout.py Tools/build_hauler.py Tools/verify_level.py Content/Maps/L_Hauler.umap Content/Materials
git commit -m "feat: place the pilot seat at the helm"
```

---

### Task 8: Wire the body, play it, record it

The human task: the anim graph, then the playtest. Then the records.

**Files:**
- Modify (in the editor): `Content/Characters/DeepSpace/ABP_DeepSpaceBody.uasset`
- Modify: `docs/decisions/0002-cpp-first-blueprints-as-wrappers.md`, `CLAUDE.md`, `docs/playtest-checklist.md`, the spec

- [ ] **Step 1: Wire `ABP_DeepSpaceBody` (the developer, in the editor)**

`./launch.sh`, then open `Content/Characters/DeepSpace/ABP_DeepSpaceBody`, **AnimGraph** tab:

1. Right-click empty space → **Blend Poses by EPosture**. Connect its output to **Output Pose**.
2. From **Active Enum Value**, drag out → **Get Posture**.
3. **Standing** pose pin → drag out → **Blend Space Player** → pick `BS_DS_Locomotion`; from its **Speed** pin → **Get Speed**.
4. **Crouched** → **Blend Space Player** → `BS_DS_Crouch`; its **Speed** ← **Get Speed**.
5. **Seated** → **Sequence Player** → `RTG_sitting_idle` (tick **Loop Animation** in Details).
6. **Falling** → **Sequence Player** → `MM_Fall_Loop` (loop).
7. Select the Blend Poses node; in Details set each **Blend Time** to `0.25`.
8. No reroute nodes; nothing in the **Event Graph**. **Compile**, **Save**.

Then: `python3 Tools/check_anim_blueprints.py` → expect `ok`. If it lists a node, remove it rather than widening the allowlist.

- [ ] **Step 2: Tune and play**

Play `L_Hauler`. Tuning lives in `BP_DeepSpaceCharacter` → **Camera**:
`EyeHeightAboveHead`, `EyeForwardOffset`, `EyeProbeRadius` (how far the eyes
stop short of a wall; must stay above the 10 cm near plane) and
`EyeHeightLagSpeed` (higher = tighter to the head's bob; lower = smoother; 0 =
rigid). They are defaults on the Blueprint — asset tuning, within the rules.

**The first playtest failed on the camera, three ways.** The spring arm is gone
as a result; see *Deviations*. Re-tune and re-play.

- [ ] **Step 3: Extend the playtest checklist**

Add to `docs/playtest-checklist.md`, before the results sections:

```markdown
## Body and movement
- [ ] Looking down shows the body; no view from inside the head or neck
- [ ] Walking keeps the arms out of view; sprinting brings them into frame
- [ ] Hold Left Shift: visibly faster forward; no faster strafing or backing up
- [ ] Camera bob while running is comfortable
- [ ] C crouches and lowers the view; C again stands
- [ ] Crouched, the crawlway can be entered and crossed, cargo bay to engineering
- [ ] Inside the crawlway, C does nothing (no room to stand)
- [ ] The view never passes through a ceiling, standing or crouched

## Pilot seat
- [ ] Looking at the port cockpit seat shows "Sit in  Pilot Seat"
- [ ] E sits: the view settles at seated height, facing the window
- [ ] Seated, the view turns about ±100° and ±70° and no further
- [ ] Seated, the prompt reads "Stand up", whatever you look at
- [ ] E stands you up behind the seat, free to walk
- [ ] The starboard seat gives no prompt
```

Run the whole checklist and record the result, dated, as a new `## Result — movement, body and pilot seat` section. Record failures as failures.

- [ ] **Step 4: Amend ADR 0002**

Append to `docs/decisions/0002-cpp-first-blueprints-as-wrappers.md`:

```markdown
## Amendment — Animation Blueprints (2026-09-20)

An Animation Blueprint is a node graph in a binary asset, which is where
logic goes to become invisible. The rule extends to them: C++ decides, the
graph wires. `UDeepSpaceAnimInstance` computes speed and posture;
`ABP_DeepSpaceBody` may contain only the output pose, blend-space and
sequence players, *Blend Poses by* nodes, variable *gets*, and the event stubs
every Animation Blueprint is created with. No state machines, no Event Graph
logic, no reroutes.

The drift signal is now mechanical: `python3 Tools/check_anim_blueprints.py`
reads each Animation Blueprint the project owns and fails on any other node
type. It is an allowlist, so a node nobody anticipated fails until someone
decides it belongs.

Setting assets on a Blueprint remains allowed, and is now scripted where
possible: `Tools/setup_character.py` assigns the character's input actions,
body mesh and anim class, so they are reviewable text, not clicks.
```

- [ ] **Step 5: Record the pipeline in `CLAUDE.md`**

Append:

````markdown
## The player's body

The body's animations come from Mixamo FBX in `SourceArt/Mixamo/`, retargeted
onto the mannequin by script. Run in this order after adding or changing a
clip, with the editor closed:

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/import_animations.py" -unattended -nopause -nosplash -NoLiveCoding
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/setup_character.py" -unattended -nopause -nosplash -NoLiveCoding
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/check_anim_heights.py" -unattended -nopause -nosplash -NoLiveCoding
python3 Tools/check_anim_blueprints.py
```

- **Blend spaces built from Python blend nothing** unless
  `unreal.DeepSpaceEditorScripting.rebuild_blend_space` is called: setting
  their samples never builds the interpolation table. `import_animations.py`
  does this; any new script that edits a blend space must too.
- **The camera rides the head bone and must stay inside the capsule.**
  `check_anim_heights.py` checks every clip's peak head height against its
  posture's capsule. A taller clip fails there, not in play.
- **The movement contract lives in `Tools/movement_contract.json`**, read by
  the layout and by the `DeepSpace.Player.MovementContract` test. Change it
  there; each side fails its own test if the other no longer fits.
- `ABP_DeepSpaceBody` is hand-wired and may only wire (ADR 0002); the import
  script updates its blend spaces in place, never deleting them, so its
  references survive a re-import.
````

- [ ] **Step 6: Update the spec's status**

In `docs/superpowers/specs/2026-09-20-movement-body-pilot-design.md`, set `**Status:**` to `Implemented — playtest passed <date>` (or `…playtest failed: see checklist` if it did).

- [ ] **Step 7: Commit**

```bash
git add Content/Characters/DeepSpace/ABP_DeepSpaceBody.uasset Content/Blueprints docs/ CLAUDE.md
git commit -m "feat: wire the body's animation; record the playtest, ADR amendment and pipeline"
```

---

## Done when

- Automation: `DeepSpace.Player.MovementContract`, `DeepSpace.Player.Sprint`, `DeepSpace.Ship.Piloted`, `DeepSpace.Ship.PowerState` pass.
- `python3 Tools/validate_hauler.py`, `test_floorplan.py`, `test_placement.py`, `check_anim_blueprints.py` pass; `check_anim_heights.py` and `verify_level.py` report PASS.
- The extended playtest checklist passes, with results recorded.
- It no longer feels like a floating camera. A human judgement, and the point.
