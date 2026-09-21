# CLAUDE.md — DeepSpace

Guidance for Claude Code when working in this repository.

## What this is

A procedurally generated space exploration game built in Unreal Engine 5.8,
emphasizing the scale of space: cruise, choose a destination, hyperjump, arrive.
The player lives aboard a ship they walk around inside and progressively upgrade.

Currently at **Milestone 1 — walk the ship**. See `docs/superpowers/plans/`.

The developer knows general game development but is new to Unreal and has no
art or modeling experience. **Explain Unreal-specific concepts rather than
assuming them.**

## Engine

Unreal Engine 5.8.2 lives at `~/UnrealEngine/UE_5.8`, outside this repo.

**Always launch via `unreal-editor`**, never the raw binary — the wrapper forces
XWayland, without which the editor renders blurry on the 4K display. See
`docs/decisions/0001-engine-and-toolchain.md`, which also records the several
places Epic's Linux documentation is wrong for the precompiled binary.

## Commands

```bash
./build.sh          # canonical compile check — run after EVERY C++ change
unreal-editor DeepSpace.uproject    # open the project

# Regenerate IDE project files (after moving files or adding modules)
~/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
    -project="$PWD/DeepSpace.uproject" -game -vscode
```

Run automation tests headlessly (preferred — no UI clicking, works over SSH):

```bash
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -ExecCmds="Automation RunTests DeepSpace" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -NoLiveCoding

# The verdict lands in the log, not on stdout:
grep "Test Completed" Saved/Logs/DeepSpace.log | tail
```

`-nullrhi` skips the renderer entirely, which is why this works without a
display. Use `-TestExit`, not `; Quit` — tests run asynchronously, so `Quit`
exits before the queue drains and the run reports nothing at all rather than
failing visibly. Tests can also be run from the editor via **Tools → Session Frontend →
Automation**, filter `DeepSpace`.

## The rule that matters most

**All gameplay logic lives in C++. Blueprints only assign assets and expose
tunable values.**

Blueprints are binary `.uasset` files: not diffable, not mergeable, unreadable
to Claude. Logic placed there is invisible to both git history and to any AI
collaborator, and Blueprint-heavy projects are where hobby games hit performance
walls.

**Drift signal:** if gameplay logic starts appearing in `Content/`, the plan has
been abandoned. Say so.

## Architecture

- `Source/DeepSpace/Ship/ShipPowerState.*` — pure C++ power arithmetic, no
  Unreal types beyond containers. Headlessly unit-testable. The complexity sink.
- `Source/DeepSpace/Ship/ShipSubsystem.*` — `UWorldSubsystem` wrapping the above;
  authoritative ship state. Knows nothing about meshes, rooms, or the player.
- `Source/DeepSpace/Ship/InteractableComponent.*` — a component, deliberately
  not a class hierarchy, bolted onto anything interactable.
- `Source/DeepSpace/Player/DeepSpaceCharacter.*` — first-person pawn and the
  interaction trace.

Consumers **ask** the subsystem for state; they never store it. That discipline
is what keeps ship state from scattering across actors.

## Version control

`.uasset`/`.umap` go through **Git LFS** — configured before the first binary
asset existed. Never hand-edit them.

`Binaries/`, `Intermediate/`, `Saved/`, `.vscode/`, `Makefile`, and
`*.code-workspace` are generated and gitignored.

## Docs

- `docs/superpowers/specs/` — design docs (the why)
- `docs/superpowers/plans/` — implementation plans, updated in place as reality
  contradicts them
- `docs/decisions/` — short ADRs

## Generated level geometry

`Content/Maps/L_Hauler.umap` is **generated, not hand-edited**. The readable
source of truth is `Tools/hauler_layout.py`; `Tools/build_hauler.py` realises it
in the editor via the bundled Python (`PythonScriptPlugin`). Actors the script
owns are prefixed `hauler_` and are destroyed and rebuilt on every run, so
hand-placed changes to them are lost. Edit the layout and re-run.

```bash
python3 Tools/validate_hauler.py        # no editor needed, ~1s
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
    -unattended -nopause -nosplash -NoLiveCoding
```

`validate_hauler.py` voxelises the layout at 10 cm and checks that the hull is
sealed, that every named region can be walked to from the Player Start with
180 cm of headroom, and that the geometry forms one connected component. Ships
will eventually be procedurally generated, so these are properties to assert,
not to eyeball. It exits non-zero and can gate a build.

`unreal.log` output does not reach stdout under the commandlet; the build writes
its summary to `Saved/hauler_build.txt`.
