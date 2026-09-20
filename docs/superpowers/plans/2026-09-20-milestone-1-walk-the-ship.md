# DeepSpace Milestone 1 — Walk the Ship — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A playable first-person build where you walk a small ship interior, interact with an engineering console, and see it display a real value sourced from ship state.

**Architecture:** All gameplay logic in C++ under `Source/DeepSpace/`. Ship state lives in a `UShipSubsystem` (a `UWorldSubsystem`) that wraps a pure, engine-independent `FShipPowerState` struct holding the arithmetic. Interactivity is a component (`UInteractableComponent`) bolted onto any actor, never a class hierarchy. Blueprints subclass C++ classes purely to assign meshes and materials.

**Tech Stack:** Unreal Engine 5.8 (precompiled Linux editor), C++ with Unreal's bundled clang toolchain, Unreal Automation Spec tests, Neovim + clangd via `compile_commands.json`, Git with Git LFS.

**Spec:** `docs/superpowers/specs/2026-09-20-deepspace-foundation-design.md`

## Global Constraints

- Engine: **Unreal Engine 5.8** (current stable, released 2026-06-17). Precompiled Linux binary, not a source build.
- Module name is **`DeepSpace`** and must not change — renaming an Unreal module after creation is manual and error-prone.
- Minimum NVIDIA driver for UE5 Linux: **570**. This machine has **610.43.03**. Satisfied.
- Minimum Vulkan: NVIDIA proprietary. This machine reports **Vulkan 1.4.341**. Satisfied.
- Disk: **39.8 GB** download, **73 GB** extracted (measured for 5.8.2 — Epic's ~43 GB figure is stale), plus project and derived data cache. Budget **~120 GB**. After verification the zip can be deleted to reclaim 39.8 GB.
- Compiler: Unreal's **bundled clang toolchain** via `SetupToolchain.sh`. Never the system clang (22.1.8) or gcc (16.1.1) — Epic targets clang 18.1.0 and mismatches produce opaque link errors.
- **No gameplay logic in `Content/`.** Blueprints assign assets and expose tunable values only. Logic appearing in `Content/` means the plan has been abandoned.
- Every `.uasset` and `.umap` goes through Git LFS from the first commit.

---

## File Structure

| File | Responsibility |
|---|---|
| `Source/DeepSpace/Ship/ShipPowerState.h/.cpp` | Pure C++ power arithmetic. No Unreal types beyond containers. Unit-testable headlessly. |
| `Source/DeepSpace/Ship/ShipSubsystem.h/.cpp` | `UWorldSubsystem` wrapping `FShipPowerState`; the authoritative, globally reachable ship state. |
| `Source/DeepSpace/Ship/ShipModuleDataAsset.h/.cpp` | Data asset describing one piece of ship equipment. |
| `Source/DeepSpace/Ship/InteractableComponent.h/.cpp` | Component making any actor interactable. |
| `Source/DeepSpace/Ship/ShipConsole.h/.cpp` | An actor that reads ship state on interaction. First real consumer of the seams. |
| `Source/DeepSpace/Player/DeepSpaceCharacter.h/.cpp` | First-person pawn: movement, camera, interaction trace. |
| `Source/DeepSpace/Core/DeepSpaceGameMode.h/.cpp` | Wires the character as the default pawn. |
| `Source/DeepSpace/Tests/ShipPowerStateTest.cpp` | Automation tests for the power math. |
| `docs/decisions/*.md` | Short ADRs capturing why, for a developer learning the engine. |

Rationale for `ShipPowerState` being separate from `ShipSubsystem`: a `UWorldSubsystem` requires a live `UWorld`, which makes headless testing awkward. Splitting the arithmetic into a plain struct makes the layer that will accumulate the most complexity the layer that is trivially testable.

---

### Task 1: Get Unreal Engine 5.8 running and verified

This task is mostly manual and gated on browser steps only the developer can perform. **Nothing else in this plan should begin until this task's verification passes.** Treat a failure here as new information that changes the plan, not as a step to push through.

**Files:**
- Create: `docs/decisions/0001-engine-and-toolchain.md`

- [x] **Step 1: Install system prerequisites** — DONE 2026-09-20 (git-lfs 3.7.1; vulkan-tools and vulkan-icd-loader 1.4.357.0 already present)

```bash
sudo pacman -S --needed git-lfs vulkan-tools vulkan-icd-loader
git lfs install
```

`git-lfs` is confirmed missing on this machine. `vulkan-tools` provides `vulkaninfo` for diagnosis.

- [ ] **Step 2: Link Epic account to GitHub and download the editor**

Manual, in a browser — Claude cannot do this:
1. Create or sign in to an Epic Games account at https://www.unrealengine.com/
2. Go to the Unreal Engine for Linux page via https://www.unrealengine.com/download
3. Download `Linux_Unreal_Engine_5.8.x.zip` (~25 GB)

- [x] **Step 3: Extract the engine** — DONE 2026-09-20

```bash
mkdir -p ~/UnrealEngine
cd ~/UnrealEngine
unzip ~/Downloads/Linux_Unreal_Engine_5.8.*.zip -d UE_5.8
du -sh ~/UnrealEngine/UE_5.8   # 5.8.2 actual: 73G, NOT the ~43G Epic's docs claim
```

Extracted to `~/UnrealEngine/` rather than into the project, so the engine is shared and never enters git.

- [x] **Step 4: Toolchain — ALREADY BUNDLED, DO NOT RUN Setup.sh**

Epic's Linux quickstart tells you to run `SetupToolchain.sh`. **That is a
source-build instruction and does not apply to the precompiled binary.**
Verified on 5.8.2, 2026-09-20:

- `Engine/Build/BatchFiles/Linux/SetupToolchain.sh` does not exist.
- The toolchain is already present at
  `Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/`
- `Setup.sh` (which does exist) is Debian-specific — it drives `dpkg-query` and
  `apt-get` — and then calls both `SetupToolchain.sh` and `BuildThirdParty.sh`,
  neither of which ships in the binary distribution. Under `set -e` it would
  fail partway. **Do not run it on Arch.**

Note the bundled clang is **20.1.8**, not the 18.1.0 Epic's system-requirements
page lists. The system clang (22.1.8) and gcc (16.1.1) remain irrelevant: the
build scripts use the bundled toolchain.

Verify with:

```bash
ls -d ~/UnrealEngine/UE_5.8/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/*/
```

- [x] **Step 5: File descriptor limit — VERIFIED, NO ACTION NEEDED**

Unreal opens a very large number of files during shader compilation and fails in
confusing ways at a low limit. The common advice is to raise it via
`/etc/security/limits.d/`.

Checked on this machine 2026-09-20: `ulimit -n` reports **524288** (systemd's
default on Arch), far above the 65536 typically recommended. No change required,
and no logout/login cycle. Also confirmed **glibc 2.44**, past the 2.35 Epic
recommends for startup performance.

- [ ] **Step 6: VERIFICATION CHECKPOINT — launch the editor**

```bash
cd ~/UnrealEngine/UE_5.8
./Engine/Binaries/Linux/UnrealEditor
```

Expected: the Unreal Project Browser window opens. First launch compiles shaders and may take several minutes with the window appearing unresponsive — this is normal.

**If it crashes or hangs:** the known NVIDIA/Vulkan workaround is running single-threaded without parallel rendering. Try:

```bash
./Engine/Binaries/Linux/UnrealEditor -onethread
```

Report which of these worked. If neither does, **stop and report** — do not proceed to Task 2. The rest of the plan assumes a working editor, and a broken one is a different problem requiring its own investigation.

- [ ] **Step 7: Record the decision**

Write `docs/decisions/0001-engine-and-toolchain.md` capturing: UE 5.8 precompiled binary chosen over a source build (hours of build time, tens of GB, only needed for engine modification); engine lives at `~/UnrealEngine/UE_5.8` outside the repo; the bundled clang toolchain is mandatory and the system clang must not be used; the nofile limit change and why; and whether `-onethread` proved necessary on this machine.

- [ ] **Step 8: Commit**

```bash
cd ~/Development/deepspace
git add docs/decisions/0001-engine-and-toolchain.md
git commit -m "docs: record engine acquisition and toolchain decisions"
```

---

### Task 2: Create the DeepSpace C++ project with clean version control

**Files:**
- Create: `DeepSpace.uproject`, `Source/DeepSpace/**` (engine-generated), `.gitignore`, `.gitattributes`, `build.sh`

**Interfaces:**
- Produces: a buildable Unreal C++ project rooted at `~/Development/deepspace`, and `./build.sh` as the canonical compile check used by every later task.

- [x] **Step 1: Create the project in the editor** — DONE 2026-09-20

**Deviation:** Starter Content was NOT enabled at creation. `Content/` is empty.
It must be added before Task 9's blockout, via Content Browser → **Add** →
**Add Feature or Content Pack** → **Content Packs** → **Starter Content**.

Original instructions:

In the Project Browser: **Games → Blank**, then set **Project Defaults → C++** (not Blueprint), **Starter Content: enabled** (it supplies placeholder meshes and materials for the blockout), and **Raytracing: disabled** for now.

Name it exactly `DeepSpace` and set the location to `/home/matt/Development/` so the project lands at `/home/matt/Development/deepspace`. If the browser creates `/home/matt/Development/DeepSpace` instead, move the generated contents into the existing `deepspace` directory — the git repo and `docs/` already there must be preserved.

The editor will compile and open the new project. Close it before the next step.

- [ ] **Step 2: Write `.gitignore`**

```gitignore
# Unreal generated — never commit
Binaries/
DerivedDataCache/
Intermediate/
Saved/
.vscode/
.vs/
*.VC.db
*.opensdf
*.opendb
*.sdf
*.sln
*.suo
*.xcodeproj
*.xcworkspace

# clangd
.cache/
compile_commands.json
```

- [ ] **Step 3: Write `.gitattributes`**

Binary types must never be merged or line-ending-converted, and large binaries go to LFS.

```gitattributes
* text=auto

*.uasset filter=lfs diff=lfs merge=lfs -text
*.umap   filter=lfs diff=lfs merge=lfs -text
*.fbx    filter=lfs diff=lfs merge=lfs -text
*.png    filter=lfs diff=lfs merge=lfs -text
*.tga    filter=lfs diff=lfs merge=lfs -text
*.wav    filter=lfs diff=lfs merge=lfs -text

*.cpp text
*.h   text
*.cs  text
*.ini text
*.uproject text
```

- [ ] **Step 4: Write `build.sh`**

```bash
#!/usr/bin/env bash
# Canonical compile check for DeepSpace. Run after every C++ change.
set -euo pipefail

UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine/UE_5.8}"
PROJECT="$(cd "$(dirname "$0")" && pwd)/DeepSpace.uproject"

"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    DeepSpaceEditor Linux Development \
    -project="$PROJECT" -waitmutex
```

```bash
chmod +x build.sh
```

- [ ] **Step 5: Verify the build script works**

Run: `./build.sh`
Expected: compiles and ends with a success line. If it reports a missing toolchain, Task 1 Step 4 did not complete.

- [ ] **Step 6: Verify LFS is actually intercepting assets**

```bash
git add -A
git lfs status
```

Expected: `.uasset` files from Starter Content appear under LFS-tracked changes. If they appear as ordinary Git objects, `.gitattributes` is wrong or `git lfs install` was not run — fix before committing, because retroactively converting committed binaries is painful.

- [ ] **Step 7: Commit**

```bash
git commit -m "feat: scaffold DeepSpace UE5 C++ project with LFS and build script"
```

---

### Task 3: VSCode code intelligence — DONE 2026-09-20

**Changed from the original plan.** The spec specified Neovim + clangd; the
developer chose VSCode instead. Both are fed by the same generated files, so
Neovim/clangd remains available later without redoing anything.

**Important:** the MS C/C++ extension and clangd **conflict** if both are active
in VSCode — you get duplicate, contradictory diagnostics. C/C++ is primary here
because it is what Unreal's own integration generates for. Do not add the clangd
extension to VSCode.

- [x] **Step 1: Install VSCode** — `yay -S visual-studio-code-bin` (1.138.0)

- [x] **Step 2: Regenerate project files**

```bash
cd ~/Development/deepspace
rm -rf .vscode DeepSpace.code-workspace
~/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
    -project="$PWD/DeepSpace.uproject" -game -vscode
```

Required after the project was moved out of its nested directory: the generated
files hold absolute paths and all six pointed at the old location.

Produces `.vscode/` (with `c_cpp_properties.json` and `compileCommands_*.json`)
and `DeepSpace.code-workspace`. All are gitignored — they are regenerated, not
authored.

- [x] **Step 3: Install extensions**

```bash
code --install-extension ms-vscode.cpptools      # C++ (v1.34.4)
code --install-extension ms-dotnettools.csharp   # for .Build.cs / .Target.cs (v2.160.4)
```

The C# extension is easy to overlook: Unreal's build files (`DeepSpace.Build.cs`,
`*.Target.cs`) are C# and get no support at all without it.

- [ ] **Step 4: Point Unreal at VSCode** (manual, in the editor)

Editor Preferences → General → **Source Code** → Source Code Editor →
**Visual Studio Code**. Makes double-clicking a C++ class in the Content Browser
open the right editor.

- [ ] **Step 5: Verify**

Open the workspace: `code ~/Development/deepspace/DeepSpace.code-workspace`

Open `Source/DeepSpace/DeepSpace.cpp`, put the cursor on an Unreal type, and
confirm go-to-definition jumps into the engine headers. First load makes
IntelliSense parse a very large header set — expect a minute or two of high CPU
before it settles.

---

### Task 4: `FShipPowerState` — the power arithmetic, test-first — DONE 2026-09-20

This is the only genuinely unit-testable layer in milestone 1, and the layer that will grow most. It is written test-first.

**Files:**
- Create: `Source/DeepSpace/Ship/ShipPowerState.h`, `Source/DeepSpace/Ship/ShipPowerState.cpp`
- Test: `Source/DeepSpace/Tests/ShipPowerStateTest.cpp`
- Modify: `Source/DeepSpace/DeepSpace.Build.cs`

**Interfaces:**
- Produces:
  - `struct FShipPowerState`
  - `void SetReactorOutput(float Watts)`
  - `float GetReactorOutput() const`
  - `bool AddDraw(FName ModuleId, float Watts)` — returns `false` if `ModuleId` is already present
  - `bool RemoveDraw(FName ModuleId)` — returns `false` if `ModuleId` was not present
  - `float GetTotalDraw() const`
  - `float GetHeadroom() const` — reactor output minus total draw; may be negative
  - `bool IsOverloaded() const` — true when headroom is negative

- [x] **Step 1: Build config**

**Deviation:** the planned `FunctionalTesting` dependency proved unnecessary —
`IMPLEMENT_SIMPLE_AUTOMATION_TEST` and `FAutomationTestBase` live in `Core`,
which is already a dependency. Not added (YAGNI).

**What was actually needed instead**, and was not in the plan:

```csharp
// UBT auto-adds include paths only for the Public/Private layout. This module
// uses a flat one (Ship/, Player/, Core/), so the module root must be declared
// or subdirectory headers cannot be included as "Ship/Foo.h".
PublicIncludePaths.Add(ModuleDirectory);
```

`EnhancedInput` was already present in the template's dependencies, so Task 7
Step 1 is pre-satisfied.

- [ ] **Step 2: Write the failing tests**

Create `Source/DeepSpace/Tests/ShipPowerStateTest.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Ship/ShipPowerState.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShipPowerStateTest,
    "DeepSpace.Ship.PowerState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShipPowerStateTest::RunTest(const FString& Parameters)
{
    // A fresh state draws nothing and has all its reactor output spare.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        TestEqual(TEXT("fresh state draws nothing"), State.GetTotalDraw(), 0.0f);
        TestEqual(TEXT("headroom equals reactor output"), State.GetHeadroom(), 1000.0f);
        TestFalse(TEXT("fresh state is not overloaded"), State.IsOverloaded());
    }

    // Draws accumulate.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        TestTrue(TEXT("first add succeeds"), State.AddDraw(TEXT("LifeSupport"), 300.0f));
        TestTrue(TEXT("second add succeeds"), State.AddDraw(TEXT("Lights"), 120.0f));
        TestEqual(TEXT("draws sum"), State.GetTotalDraw(), 420.0f);
        TestEqual(TEXT("headroom reduced"), State.GetHeadroom(), 580.0f);
    }

    // Adding the same module twice is rejected rather than silently doubling draw.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.AddDraw(TEXT("LifeSupport"), 300.0f);
        TestFalse(TEXT("duplicate add is rejected"), State.AddDraw(TEXT("LifeSupport"), 300.0f));
        TestEqual(TEXT("draw unchanged by rejected add"), State.GetTotalDraw(), 300.0f);
    }

    // Removal frees the draw; removing something absent is reported, not ignored.
    {
        FShipPowerState State;
        State.SetReactorOutput(1000.0f);
        State.AddDraw(TEXT("LifeSupport"), 300.0f);
        TestTrue(TEXT("remove succeeds"), State.RemoveDraw(TEXT("LifeSupport")));
        TestEqual(TEXT("draw released"), State.GetTotalDraw(), 0.0f);
        TestFalse(TEXT("removing absent module reports false"), State.RemoveDraw(TEXT("Nothing")));
    }

    // Exceeding reactor output is representable, not clamped away.
    {
        FShipPowerState State;
        State.SetReactorOutput(100.0f);
        State.AddDraw(TEXT("Engines"), 250.0f);
        TestEqual(TEXT("headroom goes negative"), State.GetHeadroom(), -150.0f);
        TestTrue(TEXT("state reports overloaded"), State.IsOverloaded());
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

Note the last case: overload is represented rather than prevented. Whether an overloaded ship browns out is a gameplay decision for a later milestone, and the data layer should not quietly decide it here.

- [ ] **Step 3: Run the tests to verify they fail**

Run:
```bash
./build.sh
```
Expected: FAIL at compile time — `Ship/ShipPowerState.h` does not exist.

- [ ] **Step 4: Write the header**

Create `Source/DeepSpace/Ship/ShipPowerState.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

/**
 * Pure power arithmetic for a ship. Deliberately not a UObject and not tied
 * to a UWorld, so it can be unit-tested headlessly. UShipSubsystem owns an
 * instance of this and exposes it to gameplay.
 */
struct DEEPSPACE_API FShipPowerState
{
public:
    void SetReactorOutput(float Watts);
    float GetReactorOutput() const;

    /** Returns false if ModuleId already draws power. */
    bool AddDraw(FName ModuleId, float Watts);

    /** Returns false if ModuleId was not drawing power. */
    bool RemoveDraw(FName ModuleId);

    float GetTotalDraw() const;

    /** Reactor output minus total draw. Negative when overloaded. */
    float GetHeadroom() const;

    bool IsOverloaded() const;

private:
    float ReactorOutput = 0.0f;
    TMap<FName, float> Draws;
};
```

- [ ] **Step 5: Write the implementation**

Create `Source/DeepSpace/Ship/ShipPowerState.cpp`:

```cpp
#include "Ship/ShipPowerState.h"

void FShipPowerState::SetReactorOutput(float Watts)
{
    ReactorOutput = Watts;
}

float FShipPowerState::GetReactorOutput() const
{
    return ReactorOutput;
}

bool FShipPowerState::AddDraw(FName ModuleId, float Watts)
{
    if (Draws.Contains(ModuleId))
    {
        return false;
    }
    Draws.Add(ModuleId, Watts);
    return true;
}

bool FShipPowerState::RemoveDraw(FName ModuleId)
{
    return Draws.Remove(ModuleId) > 0;
}

float FShipPowerState::GetTotalDraw() const
{
    float Total = 0.0f;
    for (const TPair<FName, float>& Draw : Draws)
    {
        Total += Draw.Value;
    }
    return Total;
}

float FShipPowerState::GetHeadroom() const
{
    return ReactorOutput - GetTotalDraw();
}

bool FShipPowerState::IsOverloaded() const
{
    return GetHeadroom() < 0.0f;
}
```

- [ ] **Step 6: Build and run the tests**

```bash
./build.sh
```

Then run headlessly — better than the Session Frontend, and what was actually used:

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace.Ship.PowerState; Quit" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding
grep "Test Completed" Saved/Logs/DeepSpace.log | tail -1
```

Expected: `Result={Success}`. Achieved 2026-09-20.

- [ ] **Step 7: Commit**

```bash
git add Source/DeepSpace/Ship/ShipPowerState.h Source/DeepSpace/Ship/ShipPowerState.cpp \
        Source/DeepSpace/Tests/ShipPowerStateTest.cpp Source/DeepSpace/DeepSpace.Build.cs
git commit -m "feat: add FShipPowerState with automation tests"
```

---

### Task 5: `UShipModuleDataAsset` and `UShipSubsystem` — DONE 2026-09-20

**Files:**
- Create: `Source/DeepSpace/Ship/ShipModuleDataAsset.h/.cpp`, `Source/DeepSpace/Ship/ShipSubsystem.h/.cpp`

**Interfaces:**
- Consumes: `FShipPowerState` from Task 4.
- Produces:
  - `UShipModuleDataAsset` with `FText DisplayName`, `FName ModuleId`, `float PowerDraw`, `TSoftObjectPtr<UStaticMesh> Mesh`
  - `UShipSubsystem : UWorldSubsystem`
  - `static UShipSubsystem* Get(const UObject* WorldContext)`
  - `bool InstallModule(UShipModuleDataAsset* Module)`
  - `bool RemoveModule(UShipModuleDataAsset* Module)`
  - `float GetPowerDraw() const`
  - `float GetPowerHeadroom() const`
  - `float GetReactorOutput() const`
  - `bool IsPowerOverloaded() const`

- [x] **Step 1: Write the data asset header**

Create `Source/DeepSpace/Ship/ShipModuleDataAsset.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ShipModuleDataAsset.generated.h"

/**
 * Describes one piece of ship equipment. Milestone 1 defines only a few of
 * these and uses little of the data; the shape existing is the point, so
 * that upgrades and procedural generation later write data rather than code.
 */
UCLASS(BlueprintType)
class DEEPSPACE_API UShipModuleDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** Stable identifier used as the key in power bookkeeping. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FName ModuleId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FText DisplayName;

    /** Continuous power draw in watts while installed. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    float PowerDraw = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    TSoftObjectPtr<UStaticMesh> Mesh;
};
```

- [x] **Step 2: Write the data asset source**

Create `Source/DeepSpace/Ship/ShipModuleDataAsset.cpp`:

```cpp
#include "Ship/ShipModuleDataAsset.h"
```

The class is pure data; no implementation is needed beyond the translation unit.

- [x] **Step 3: Write the subsystem header**

Create `Source/DeepSpace/Ship/ShipSubsystem.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Ship/ShipPowerState.h"
#include "ShipSubsystem.generated.h"

class UShipModuleDataAsset;

/**
 * Authoritative ship state. Knows nothing about meshes, rooms, or the player.
 * Everything visible reads from this; nothing else stores ship state.
 *
 * A subsystem rather than an actor: created and destroyed with the world
 * automatically, globally reachable without a singleton, and unable to
 * accidentally acquire a transform and become a god-actor.
 */
UCLASS()
class DEEPSPACE_API UShipSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    /** Convenience accessor. Returns nullptr if there is no world. */
    static UShipSubsystem* Get(const UObject* WorldContext);

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /** Returns false if the module is null or already installed. */
    UFUNCTION(BlueprintCallable, Category = "Ship")
    bool InstallModule(UShipModuleDataAsset* Module);

    /** Returns false if the module is null or was not installed. */
    UFUNCTION(BlueprintCallable, Category = "Ship")
    bool RemoveModule(UShipModuleDataAsset* Module);

    UFUNCTION(BlueprintPure, Category = "Ship")
    float GetPowerDraw() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    float GetPowerHeadroom() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    float GetReactorOutput() const;

    UFUNCTION(BlueprintPure, Category = "Ship")
    bool IsPowerOverloaded() const;

private:
    FShipPowerState PowerState;

    UPROPERTY()
    TArray<TObjectPtr<UShipModuleDataAsset>> InstalledModules;

    /** Placeholder reactor rating for milestone 1. Becomes a module later. */
    static constexpr float DefaultReactorOutput = 1000.0f;
};
```

- [x] **Step 4: Write the subsystem source**

Create `Source/DeepSpace/Ship/ShipSubsystem.cpp`:

```cpp
#include "Ship/ShipSubsystem.h"

#include "Engine/World.h"
#include "Ship/ShipModuleDataAsset.h"

UShipSubsystem* UShipSubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext)
    {
        return nullptr;
    }
    const UWorld* World = WorldContext->GetWorld();
    return World ? World->GetSubsystem<UShipSubsystem>() : nullptr;
}

void UShipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    PowerState.SetReactorOutput(DefaultReactorOutput);
}

bool UShipSubsystem::InstallModule(UShipModuleDataAsset* Module)
{
    if (!Module)
    {
        return false;
    }
    if (!PowerState.AddDraw(Module->ModuleId, Module->PowerDraw))
    {
        return false;
    }
    InstalledModules.Add(Module);
    return true;
}

bool UShipSubsystem::RemoveModule(UShipModuleDataAsset* Module)
{
    if (!Module)
    {
        return false;
    }
    if (!PowerState.RemoveDraw(Module->ModuleId))
    {
        return false;
    }
    InstalledModules.Remove(Module);
    return true;
}

float UShipSubsystem::GetPowerDraw() const
{
    return PowerState.GetTotalDraw();
}

float UShipSubsystem::GetPowerHeadroom() const
{
    return PowerState.GetHeadroom();
}

float UShipSubsystem::GetReactorOutput() const
{
    return PowerState.GetReactorOutput();
}

bool UShipSubsystem::IsPowerOverloaded() const
{
    return PowerState.IsOverloaded();
}
```

- [x] **Step 5: Build**

Run: `./build.sh`
Expected: success.

- [x] **Step 6: Create three module data assets in the editor** — DONE 2026-09-20

In the Content Browser, create a `Content/Ship/Modules/` folder. Right-click → **Miscellaneous → Data Asset** → choose `ShipModuleDataAsset`, three times:

| Asset name | ModuleId | DisplayName | PowerDraw |
|---|---|---|---|
| `DA_LifeSupport` | `LifeSupport` | Life Support | 300 |
| `DA_Lights` | `Lights` | Interior Lighting | 120 |
| `DA_Sensors` | `Sensors` | Sensor Array | 200 |

Leave `Mesh` unset — nothing reads it in milestone 1.

- [x] **Step 7: Commit**

```bash
git add Source/DeepSpace/Ship/ Content/Ship/
git commit -m "feat: add ship module data assets and UShipSubsystem"
```

---

### Task 6: `UInteractableComponent` — DONE 2026-09-20

**Files:**
- Create: `Source/DeepSpace/Ship/InteractableComponent.h/.cpp`

**Interfaces:**
- Produces:
  - `UInteractableComponent : UActorComponent`
  - `FText DisplayName`, `FText InteractionVerb`, `bool bInteractionEnabled`
  - `FOnInteracted` multicast delegate, signature `(AActor* Instigator)`
  - `FText GetPrompt() const` — e.g. "Power on  Engineering Console"
  - `bool CanInteract() const`
  - `void Interact(AActor* Instigator)`

- [x] **Step 1: Write the header**

Create `Source/DeepSpace/Ship/InteractableComponent.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInteracted, AActor*, Instigator);

/**
 * Makes any actor interactable. Deliberately a component rather than a base
 * class: doors, consoles and levers compose this instead of inheriting from
 * a hierarchy that would calcify as the ship grows.
 */
UCLASS(ClassGroup = (DeepSpace), meta = (BlueprintSpawnableComponent))
class DEEPSPACE_API UInteractableComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UInteractableComponent();

    /** What the thing is called, e.g. "Engineering Console". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    FText DisplayName;

    /** What interacting does, e.g. "Power on". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    FText InteractionVerb;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interaction")
    bool bInteractionEnabled = true;

    /** Broadcast when this component is successfully interacted with. */
    UPROPERTY(BlueprintAssignable, Category = "Interaction")
    FOnInteracted OnInteracted;

    /** Player-facing prompt combining verb and name. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FText GetPrompt() const;

    UFUNCTION(BlueprintPure, Category = "Interaction")
    bool CanInteract() const;

    /** Broadcasts OnInteracted if interaction is currently allowed. */
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    void Interact(AActor* Instigator);
};
```

- [x] **Step 2: Write the source**

Create `Source/DeepSpace/Ship/InteractableComponent.cpp`:

```cpp
#include "Ship/InteractableComponent.h"

UInteractableComponent::UInteractableComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

FText UInteractableComponent::GetPrompt() const
{
    return FText::Format(
        NSLOCTEXT("DeepSpace", "InteractPrompt", "{0}  {1}"),
        InteractionVerb,
        DisplayName);
}

bool UInteractableComponent::CanInteract() const
{
    return bInteractionEnabled;
}

void UInteractableComponent::Interact(AActor* Instigator)
{
    if (!CanInteract())
    {
        return;
    }
    OnInteracted.Broadcast(Instigator);
}
```

Ticking is disabled because this component is purely reactive; leaving tick on for every interactable in the ship is wasted work.

- [x] **Step 3: Build**

Run: `./build.sh`
Expected: success.

- [x] **Step 4: Commit**

```bash
git add Source/DeepSpace/Ship/InteractableComponent.h Source/DeepSpace/Ship/InteractableComponent.cpp
git commit -m "feat: add UInteractableComponent"
```

---

### Task 7: `ADeepSpaceCharacter` — first-person pawn with interaction trace — CODE DONE 2026-09-20 (Steps 7–8 pending, editor-only)

**Files:**
- Create: `Source/DeepSpace/Player/DeepSpaceCharacter.h/.cpp`, `Source/DeepSpace/Core/DeepSpaceGameMode.h/.cpp`
- Modify: `Source/DeepSpace/DeepSpace.Build.cs`, `Config/DefaultInput.ini`

**Interfaces:**
- Consumes: `UInteractableComponent` from Task 6.
- Produces:
  - `ADeepSpaceCharacter : ACharacter`
  - `UInteractableComponent* GetFocusedInteractable() const`
  - `FText GetCurrentPrompt() const` — empty when nothing is focused
  - `ADeepSpaceGameMode : AGameModeBase`

- [x] **Step 1: Add the Enhanced Input dependency** — already present in `DeepSpace.Build.cs`

In `Source/DeepSpace/DeepSpace.Build.cs`, add `"EnhancedInput"` to `PublicDependencyModuleNames`.

- [x] **Step 2: Write the character header**

Create `Source/DeepSpace/Player/DeepSpaceCharacter.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DeepSpaceCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UInteractableComponent;
struct FInputActionValue;

/**
 * First-person pawn. Owns movement, the camera, and the trace that finds the
 * interactable the player is looking at.
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

    /** Prompt for the focused interactable, or empty text if there is none. */
    UFUNCTION(BlueprintPure, Category = "Interaction")
    FText GetCurrentPrompt() const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;

    /** How far the player can reach, in centimetres. */
    UPROPERTY(EditDefaultsOnly, Category = "Interaction")
    float InteractionRange = 250.0f;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> DefaultMappingContext;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> MoveAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> JumpAction;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> InteractAction;

private:
    void Move(const FInputActionValue& Value);
    void Look(const FInputActionValue& Value);
    void TryInteract();

    /** Re-runs the reach trace and updates FocusedInteractable. */
    void UpdateFocusedInteractable();

    UPROPERTY()
    TObjectPtr<UInteractableComponent> FocusedInteractable;
};
```

- [x] **Step 3: Write the character source**

Create `Source/DeepSpace/Player/DeepSpaceCharacter.cpp`:

```cpp
#include "Player/DeepSpaceCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Ship/InteractableComponent.h"

ADeepSpaceCharacter::ADeepSpaceCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    // Roughly eye height for a standing adult, in centimetres.
    FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
    FirstPersonCamera->bUsePawnControlRotation = true;

    // Corridors are tight; a slower walk reads better than the engine default.
    GetCharacterMovement()->MaxWalkSpeed = 300.0f;
}

void ADeepSpaceCharacter::BeginPlay()
{
    Super::BeginPlay();

    if (const APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            if (DefaultMappingContext)
            {
                Subsystem->AddMappingContext(DefaultMappingContext, 0);
            }
        }
    }
}

void ADeepSpaceCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
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
    }
    if (LookAction)
    {
        Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ADeepSpaceCharacter::Look);
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
}

void ADeepSpaceCharacter::Move(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    if (!Controller)
    {
        return;
    }
    AddMovementInput(GetActorForwardVector(), Axis.Y);
    AddMovementInput(GetActorRightVector(), Axis.X);
}

void ADeepSpaceCharacter::Look(const FInputActionValue& Value)
{
    const FVector2D Axis = Value.Get<FVector2D>();
    AddControllerYawInput(Axis.X);
    AddControllerPitchInput(-Axis.Y);
}

void ADeepSpaceCharacter::UpdateFocusedInteractable()
{
    FocusedInteractable = nullptr;

    if (!FirstPersonCamera)
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
    return FocusedInteractable ? FocusedInteractable->GetPrompt() : FText::GetEmpty();
}
```

The trace runs every tick. That is fine for one player and a handful of interactables; if it ever shows up in a profile, the fix is to trace on an interval rather than to restructure.

- [x] **Step 4: Write the game mode header**

Create `Source/DeepSpace/Core/DeepSpaceGameMode.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DeepSpaceGameMode.generated.h"

UCLASS()
class DEEPSPACE_API ADeepSpaceGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ADeepSpaceGameMode();
};
```

- [x] **Step 5: Write the game mode source**

Create `Source/DeepSpace/Core/DeepSpaceGameMode.cpp`:

```cpp
#include "Core/DeepSpaceGameMode.h"

#include "Player/DeepSpaceCharacter.h"

ADeepSpaceGameMode::ADeepSpaceGameMode()
{
    DefaultPawnClass = ADeepSpaceCharacter::StaticClass();
}
```

- [x] **Step 6: Build**

Run: `./build.sh`
Expected: success.

The first attempt failed: `Character.h` only forward-declares `UCapsuleComponent`,
so `SetupAttachment(GetCapsuleComponent())` cannot convert it to `USceneComponent*`.
`#include "Components/CapsuleComponent.h"` is required and has been added above.

- [ ] **Step 7: Create the Enhanced Input assets in the editor** — BLOCKED: requires the editor GUI

In `Content/Input/`, create:
- Input Actions (right-click → **Input → Input Action**): `IA_Move` (Value Type: Axis2D), `IA_Look` (Axis2D), `IA_Jump` (Digital Bool), `IA_Interact` (Digital Bool)
- An Input Mapping Context `IMC_Default` binding:
  - `IA_Move` → W/A/S/D, with modifiers: W plain; S with *Negate*; A with *Swizzle Axis (YXZ)* + *Negate*; D with *Swizzle Axis (YXZ)*
  - `IA_Look` → Mouse XY 2D-Axis, with *Negate* on the Y component
  - `IA_Jump` → Space Bar
  - `IA_Interact` → E

- [ ] **Step 8: Create the Blueprint wrappers** — BLOCKED: requires the editor GUI

In `Content/Blueprints/`, create `BP_DeepSpaceCharacter` subclassing `DeepSpaceCharacter`, and set its `DefaultMappingContext`, `MoveAction`, `LookAction`, `JumpAction`, and `InteractAction` to the assets from Step 7.

Create `BP_DeepSpaceGameMode` subclassing `DeepSpaceGameMode` and set **Default Pawn Class** to `BP_DeepSpaceCharacter`.

This is exactly the intended division: the Blueprints carry asset references, not logic.

- [ ] **Step 9: Commit**

```bash
git add Source/DeepSpace/Player/ Source/DeepSpace/Core/ Source/DeepSpace/DeepSpace.Build.cs Content/Input/ Content/Blueprints/
git commit -m "feat: add first-person character with interaction trace and game mode"
```

---

### Task 8: `AShipConsole` — the first real consumer of ship state

**Files:**
- Create: `Source/DeepSpace/Ship/ShipConsole.h/.cpp`

**Interfaces:**
- Consumes: `UShipSubsystem` (Task 5), `UInteractableComponent` (Task 6).
- Produces:
  - `AShipConsole : AActor`
  - `FText GetReadout() const` — the text a screen material displays
  - `bool bIsPowered`
  - Blueprint event `OnReadoutChanged()` for the Blueprint to refresh its display

- [ ] **Step 1: Write the header**

Create `Source/DeepSpace/Ship/ShipConsole.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipConsole.generated.h"

class UInteractableComponent;
class UStaticMeshComponent;

/**
 * An engineering console. Reads power figures from UShipSubsystem on demand
 * and never stores them — the discipline that keeps ship state from
 * scattering across actors.
 */
UCLASS()
class DEEPSPACE_API AShipConsole : public AActor
{
    GENERATED_BODY()

public:
    AShipConsole();

    /** Text for the screen. Queries the subsystem fresh on every call. */
    UFUNCTION(BlueprintPure, Category = "Console")
    FText GetReadout() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    bool bIsPowered = false;

    /** Implemented in Blueprint to update the screen material. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Console")
    void OnReadoutChanged();

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UInteractableComponent> Interactable;

private:
    UFUNCTION()
    void HandleInteracted(AActor* Instigator);
};
```

- [ ] **Step 2: Write the source**

Create `Source/DeepSpace/Ship/ShipConsole.cpp`:

```cpp
#include "Ship/ShipConsole.h"

#include "Components/StaticMeshComponent.h"
#include "Ship/InteractableComponent.h"
#include "Ship/ShipSubsystem.h"

AShipConsole::AShipConsole()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = Mesh;

    Interactable = CreateDefaultSubobject<UInteractableComponent>(TEXT("Interactable"));
    Interactable->DisplayName = NSLOCTEXT("DeepSpace", "EngConsole", "Engineering Console");
    Interactable->InteractionVerb = NSLOCTEXT("DeepSpace", "PowerOn", "Power on");
}

void AShipConsole::BeginPlay()
{
    Super::BeginPlay();
    Interactable->OnInteracted.AddDynamic(this, &AShipConsole::HandleInteracted);
}

void AShipConsole::HandleInteracted(AActor* Instigator)
{
    bIsPowered = !bIsPowered;

    Interactable->InteractionVerb = bIsPowered
        ? NSLOCTEXT("DeepSpace", "PowerOff", "Power off")
        : NSLOCTEXT("DeepSpace", "PowerOn", "Power on");

    OnReadoutChanged();
}

FText AShipConsole::GetReadout() const
{
    if (!bIsPowered)
    {
        return NSLOCTEXT("DeepSpace", "ConsoleOffline", "OFFLINE");
    }

    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship)
    {
        return NSLOCTEXT("DeepSpace", "ConsoleNoShip", "NO SIGNAL");
    }

    return FText::Format(
        NSLOCTEXT("DeepSpace", "ConsoleReadout", "DRAW {0} W / {1} W\nHEADROOM {2} W"),
        FText::AsNumber(FMath::RoundToInt(Ship->GetPowerDraw())),
        FText::AsNumber(FMath::RoundToInt(Ship->GetReactorOutput())),
        FText::AsNumber(FMath::RoundToInt(Ship->GetPowerHeadroom())));
}
```

- [ ] **Step 3: Build**

Run: `./build.sh`
Expected: success.

- [ ] **Step 4: Create the console Blueprint**

In `Content/Blueprints/`, create `BP_ShipConsole` subclassing `ShipConsole`. Assign a Starter Content mesh to `Mesh` (a small cube scaled into a panel is fine). Add a Text Render component or a screen plane, and implement the `OnReadoutChanged` event to set its text from `GetReadout()`. Also call `GetReadout()` on Begin Play so it shows `OFFLINE` initially.

- [ ] **Step 5: Commit**

```bash
git add Source/DeepSpace/Ship/ShipConsole.h Source/DeepSpace/Ship/ShipConsole.cpp Content/Blueprints/
git commit -m "feat: add ship console reading live power state"
```

---

### Task 9: The hauler interior and the playtest gate

This is the task with the most editor work and the least code. It is where milestone 1 becomes real.

**Files:**
- Create: `Content/Maps/L_Hauler.umap`, `docs/playtest-checklist.md`

**Interfaces:**
- Consumes: everything from Tasks 5–8.

- [ ] **Step 1: Create the level**

New Level → **Empty Open World** is unnecessary here; use **Basic**, then delete the floor and default sky if they conflict with an enclosed interior.

- [ ] **Step 2: Block out the hauler**

Using Starter Content geometry or BSP boxes, build four connected spaces with a walkable path between them:

| Space | Approximate size | Contains |
|---|---|---|
| Cockpit | 3 m × 3 m | A forward window opening |
| Corridor | 1.5 m wide × 8 m | Connects everything |
| Engineering | 3 m × 4 m | `BP_ShipConsole` on a wall |
| Bunk | 2.5 m × 3 m | A bed-sized block |

Unreal's default unit is centimetres, so 3 m is 300 units. Keep ceilings at least 2.5 m or the first-person camera will feel oppressive. Doorways should be at least 100 units wide — narrower reads as realistic but plays badly.

- [ ] **Step 3: Add stars outside the window**

Place a Sky Sphere or a large inverted sphere with an emissive starfield material, or simply a black background with a few emissive points. Milestone 1 requires only that looking out the window does not show void or the default blue sky.

- [ ] **Step 4: Set the game mode and player start**

Place a Player Start in the corridor. In **World Settings**, set **GameMode Override** to `BP_DeepSpaceGameMode`.

- [ ] **Step 5: Install modules at startup so the console shows real numbers**

In the Level Blueprint, on Begin Play, get the Ship Subsystem and call `InstallModule` for `DA_LifeSupport`, `DA_Lights`, and `DA_Sensors`.

This is the one acceptable use of the Level Blueprint in milestone 1 — it is scene setup, not gameplay logic. When a real ship-configuration system arrives it replaces this, and that replacement is the point of the data-asset seam.

Expected readout when powered: `DRAW 620 W / 1000 W` and `HEADROOM 380 W`.

- [ ] **Step 6: Write the playtest checklist**

Create `docs/playtest-checklist.md`:

```markdown
# DeepSpace Playtest Checklist — Milestone 1

Run before declaring milestone 1 complete. Manual; there is no automated
substitute for embodied behavior.

## Movement and collision
- [ ] Spawn in the corridor, standing, camera at plausible eye height
- [ ] Walk the full ship: corridor → cockpit → corridor → engineering → bunk
- [ ] No falling through floors anywhere
- [ ] No getting stuck on doorframes or wall seams
- [ ] Cannot walk through walls
- [ ] Cannot escape the ship interior
- [ ] Jump does not clip through the ceiling

## Camera
- [ ] Mouse look is smooth, correct direction on both axes
- [ ] Looking straight up and straight down does not flip or invert
- [ ] Camera never clips inside geometry while walking

## View
- [ ] Cockpit window shows stars, not void or default sky

## Interaction
- [ ] Approaching the console within reach shows a prompt
- [ ] Prompt disappears when looking away
- [ ] Prompt disappears when standing too far away
- [ ] Pressing E toggles the console
- [ ] Powered console reads DRAW 620 W / 1000 W, HEADROOM 380 W
- [ ] Unpowered console reads OFFLINE
- [ ] Verb changes between "Power on" and "Power off"
- [ ] No other surface in the ship produces a prompt

## Stability
- [ ] Play for two minutes continuously without a crash or hitch
```

- [ ] **Step 7: Run the checklist**

Play in Editor and work through every item. **Record actual results, including failures.** A failed item is information, not something to quietly fix and re-declare passing — note it, fix it, and re-run the affected section.

- [ ] **Step 8: Run the automation tests one more time**

Editor → **Tools → Session Frontend → Automation** → run `DeepSpace.Ship.PowerState`.
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add Content/Maps/ docs/playtest-checklist.md
git commit -m "feat: add hauler interior level and playtest checklist"
```

---

### Task 10: Project documentation

**Files:**
- Create: `CLAUDE.md`, `docs/decisions/0002-cpp-first-blueprints-as-wrappers.md`, `docs/decisions/0003-ship-state-as-subsystem.md`, `README.md`

- [ ] **Step 1: Write `CLAUDE.md`**

Cover: what the project is; that the engine lives at `~/UnrealEngine/UE_5.8` and is not in the repo; that `./build.sh` is the canonical compile check after every C++ change; the C++-vs-Blueprint rule and its drift signal; that `.uasset`/`.umap` are LFS and must never be hand-edited; where the spec and plans live; and that the developer is learning Unreal, so Unreal-specific concepts should be explained rather than assumed.

- [ ] **Step 2: Write ADR 0002**

Why C++ holds logic and Blueprints only assign assets: Blueprints are binary, undiffable, unmergeable, and unreadable to Claude; Blueprint-heavy projects hit performance walls. Record the drift signal.

- [ ] **Step 3: Write ADR 0003**

Why ship state is a `UWorldSubsystem` wrapping a plain struct: lifetime managed with the world, globally reachable without a singleton, cannot become a god-actor; and the arithmetic lives in `FShipPowerState` so it is testable without a live `UWorld`.

- [ ] **Step 4: Write `README.md`**

Short: what the project is, prerequisites, how to get set up pointing at ADR 0001, how to build, how to run tests, how to play.

- [ ] **Step 5: Commit**

```bash
git add CLAUDE.md README.md docs/decisions/
git commit -m "docs: add project instructions, README, and architecture decision records"
```

---

## Milestone 1 Complete

Done when: the playtest checklist passes end to end, `./build.sh` succeeds from clean, and `DeepSpace.Ship.PowerState` passes.

The next design pass covers ship systems with real consequence — giving the player a reason to care what the console says. That gets its own brainstorming and spec before any code.
