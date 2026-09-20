# DeepSpace — Session Handoff

**Written:** 2026-09-20
**Picks up at:** Task 5, Step 6 (create three data assets in the editor)

## What this project is

A procedurally generated space exploration game in Unreal Engine 5.8,
emphasizing the scale of space: cruise, pick a destination, hyperjump, arrive.
You live aboard a ship you walk around inside and upgrade over time.

**Milestone 1** is deliberately small: walk around a four-room hauler, approach
an engineering console, interact with it, and see it display a real power figure
sourced from ship state. No flying, no planets, no procgen yet.

The developer is new to Unreal and has no art experience — **explain
Unreal-specific concepts rather than assuming them.**

## Read these first, in order

1. `CLAUDE.md` — working rules for this repo
2. `docs/superpowers/specs/2026-09-20-deepspace-foundation-design.md` — the why
3. `docs/superpowers/plans/2026-09-20-milestone-1-walk-the-ship.md` — the how,
   10 tasks, updated in place as reality contradicted it
4. `docs/decisions/0001-engine-and-toolchain.md` — hard-won Linux setup facts

## Status

| Task | State |
|---|---|
| 1. Engine install + verification | **Done** |
| 2. Project scaffold, LFS, build.sh | **Done** |
| 3. VSCode setup | **Done** (except Editor Preferences step below) |
| 4. `FShipPowerState` + tests | **Done**, tests green |
| 5. `UShipModuleDataAsset` + `UShipSubsystem` | **Code done**, data assets pending |
| 6. `UInteractableComponent` | Not started |
| 7. `ADeepSpaceCharacter` + game mode | Not started |
| 8. `AShipConsole` | Not started |
| 9. Hauler level + playtest | Not started |
| 10. Docs + ADRs | Partially done (CLAUDE.md, ADR 0001 written) |

Build is green. Automation tests pass. Nine commits, all on `master`.

## Immediate next steps

### Pending manual editor work (blocking nothing except Task 9)

1. **Editor Preferences → General → Source Code → Source Code Editor →
   Visual Studio Code**
2. **Add Starter Content** — Content Browser → Add → Add Feature or Content Pack
   → Content Packs → Starter Content. It was *not* enabled at project creation,
   so `Content/` is empty. Task 9's level blockout needs these meshes.

### Task 5, Step 6 — create three data assets

Content Browser → `Content/Ship/Modules/` → right-click → Miscellaneous →
Data Asset → `ShipModuleDataAsset`:

| Asset | ModuleId | DisplayName | PowerDraw |
|---|---|---|---|
| `DA_LifeSupport` | `LifeSupport` | Life Support | 300 |
| `DA_Lights` | `Lights` | Interior Lighting | 120 |
| `DA_Sensors` | `Sensors` | Sensor Array | 200 |

Leave `Mesh` unset — nothing reads it in milestone 1. Total draw of 620 W
against a 1000 W reactor is what the console should read in Task 9.

Then continue with Task 6 from the plan.

## Commands

```bash
cd ~/Development/deepspace

./build.sh          # canonical compile check — after EVERY C++ change

# Open the editor (see the launcher warning below)
unreal-editor DeepSpace.uproject

# Run tests headlessly
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace; Quit" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding
grep "Test Completed" Saved/Logs/DeepSpace.log | tail

# Regenerate IDE files (after moving files or adding modules)
~/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
    -project="$PWD/DeepSpace.uproject" -game -vscode
```

## Traps that already cost time

- **Always launch via `unreal-editor`**, never `Engine/Binaries/Linux/UnrealEditor`.
  The wrapper forces `SDL_VIDEODRIVER=x11`; without it the editor renders blurry
  on the 4K display because Unreal's SDL Wayland backend is not HiDPI-aware.
- **Epic's Linux docs describe source builds.** `SetupToolchain.sh` does not
  exist in the binary distribution, `Setup.sh` is Debian-only and would fail
  under `set -e`, and `GenerateProjectFiles.sh` lives under
  `Engine/Build/BatchFiles/Linux/`, not at the engine root. The clang toolchain
  is already bundled.
- **`PublicIncludePaths.Add(ModuleDirectory)`** in `DeepSpace.Build.cs` is load
  bearing. UBT only auto-adds include paths for the `Public/`/`Private/` layout,
  and this module uses a flat one. Without it, `#include "Ship/Foo.h"` fails.
- **Restart the editor after adding new C++ classes.** A running editor holds
  the previously compiled module and will not show new `UCLASS` types.
- **When the editor is open, headless test runs write to `DeepSpace_2.log`**,
  not `DeepSpace.log` — the running instance holds the lock. Check both.
- **The CEF/ANGLE/EGL error wall at every launch is cosmetic.** It is Unreal's
  embedded Chromium failing to get hardware GL and falling back to software. It
  does not affect the viewport, which uses Unreal's own Vulkan RHI.
- **Desktop entries need absolute paths in `Exec=`.** A bare command works from
  a shell but fails silently from the application launcher.

## The rule that matters most

**All gameplay logic in C++. Blueprints only assign assets and expose tunable
values.** Blueprints are binary `.uasset` files — undiffable, unmergeable, and
invisible to an AI collaborator.

**Drift signal:** gameplay logic appearing in `Content/` means the plan has been
abandoned. Say so out loud.

## Architectural shape

```
FShipPowerState   pure C++ arithmetic, no UObject, headlessly testable
      ↑ owned by
UShipSubsystem    UWorldSubsystem — authoritative ship state
      ↑ queried by
AShipConsole      asks for power on demand; never stores it
      ↑ made interactable by
UInteractableComponent   a component, deliberately not a class hierarchy
      ↑ found by
ADeepSpaceCharacter      first-person pawn, traces ahead for interactables
```

Consumers **ask** the subsystem; they never cache. That discipline is what keeps
ship state from scattering across actors as the game grows.
