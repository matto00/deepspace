# DeepSpace

A procedurally generated space exploration game in Unreal Engine 5.8, built
around the scale of space: cruise, choose a destination, hyperjump, arrive. You
live aboard a ship you walk around inside and progressively upgrade.

**Status:** Milestone 1 — walk the ship. A first-person walk through a small
hauler, with an engineering console that reads live ship state.

## Prerequisites

- Linux (developed on Arch with Hyprland)
- Unreal Engine **5.8.2**, precompiled binary, at `~/UnrealEngine/UE_5.8`
- Git LFS, installed before cloning — every `.uasset` and `.umap` lives in LFS

Getting the engine running on Linux involves several steps where Epic's
documentation is wrong for the precompiled binary. Read
[ADR 0001](docs/decisions/0001-engine-and-toolchain.md) before installing.

## Build

```bash
./build.sh                       # compile check; run after every C++ change
./rebuild.sh --force --launch    # clean rebuild, then open the editor
```

Close the editor before building if you changed a header. Linux has no Live
Coding, so reflection changes need a restart — `CLAUDE.md` explains why.

## Play

```bash
unreal-editor DeepSpace.uproject
```

Open `Content/Maps/L_Hauler` and press **Play**. WASD to move, mouse to look,
Space to jump, **E** to interact. The engineering console is on the far wall of
the engineering bay.

`unreal-editor` is a local wrapper that forces XWayland for sharp rendering on
HiDPI displays; see ADR 0001.

## The level is generated

`L_Hauler` is built from `Tools/hauler_layout.py`, not placed by hand:

```bash
python3 Tools/validate_hauler.py     # checks the layout, no editor needed
```

See [ADR 0004](docs/decisions/0004-generated-level-geometry.md) and `CLAUDE.md`
for the build and verify commands.

## Test

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding
grep "Test Completed" Saved/Logs/DeepSpace.log | tail
```

Embodied behaviour — collision, reach, camera — is covered by the manual
[playtest checklist](docs/playtest-checklist.md).

## Layout

| Path | What |
|---|---|
| `Source/DeepSpace/Ship/` | Ship state, modules, the console, interaction |
| `Source/DeepSpace/Player/` | First-person character |
| `Source/DeepSpace/Core/` | Game mode |
| `Tools/` | Level generation and validation |
| `docs/superpowers/specs/` | Design documents — the why |
| `docs/superpowers/plans/` | Implementation plans, updated as reality intrudes |
| `docs/decisions/` | Architecture decision records |

All gameplay logic is C++; Blueprints only assign assets. See
[ADR 0002](docs/decisions/0002-cpp-first-blueprints-as-wrappers.md).
