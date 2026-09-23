# CLAUDE.md — DeepSpace

Guidance for Claude Code when working in this repository.

## What this is

A procedurally generated space exploration game built in Unreal Engine 5.8,
emphasizing the scale of space: cruise, choose a destination, hyperjump, arrive.
The player lives aboard a ship they walk around inside and progressively upgrade.

Currently at **Milestone 1 — walk the ship**. See `docs/superpowers/plans/`.

**Read `docs/vision.md` before proposing any design.** It records what the game
is *for* — the register, the principle that scale is only felt in contrast, and
two rules that are easy to break by accident. The first is the **anti-chore
principle**: nothing may feel like a chore or a nuisance, the test being
whether the game ever tells the player they are *behind*. It rules out
optimisation loops and threats-on-a-timer alike, and it does **not** rule out
danger or difficulty. The second is that company aboard a ship is shared
presence, never division of labour. A feature can be fun in isolation and
still be wrong against both. If a decision contradicts the vision, change the
vision deliberately rather than working around it.

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
./rebuild.sh        # clean rebuild; use when the editor says the module is stale
./rebuild.sh --force --launch   # close the editor, rebuild, reopen it
./launch.sh         # open DeepSpace: rebuilds first only if C++ is stale; focuses an open editor
unreal-editor DeepSpace.uproject    # open the project

# Regenerate IDE project files (after moving files or adding modules)
~/UnrealEngine/UE_5.8/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh \
    -project="$PWD/DeepSpace.uproject" -game -vscode
```

**`./build.sh` is only fully effective with the editor closed.** While the
editor runs it holds `libUnrealEditor-DeepSpace.so` mapped, so UBT emits
numbered hot-reload copies (`-0001`, `-0002`, …) and leaves
`Binaries/Linux/UnrealEditor.modules` pointing at the original. The editor then
loads the stale library and asks for a manual rebuild — which produces yet
another numbered copy. `./rebuild.sh` refuses to run while the editor is open
(`--force` kills it), clears the stray libraries, builds, and verifies the
manifest names a library newer than the newest source.

**The restart cannot be avoided on Linux for most C++ changes.** Live Coding,
Unreal's in-place patcher, is Windows-only — its build rule is gated on `Win64`
and no Linux binary ships. Linux has only the older Hot Reload, which handles
edits *inside function bodies* but not reflection changes: a new `UPROPERTY`,
`UFUNCTION`, component or class changes a layout that Blueprints were already
built against. Treat any header change as needing `./rebuild.sh --force --launch`.

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

- `docs/vision.md` — what the game is for; the measure other docs answer to
- `docs/superpowers/specs/` — design docs (the why)
- `docs/superpowers/plans/` — implementation plans, updated in place as reality
  contradicts them
- `docs/decisions/` — short ADRs

## Randomness

**Reach for the distribution that describes the thing; do not default to
uniform.** Uniform means "every value in this range is equally likely", which
is true of almost nothing in a world, and reaching for it by habit is how
generated content comes to feel generated. Exponential for gaps between events,
Poisson for counts in an interval, Weibull for wear and failure, log-normal for
magnitudes built from many factors, Beta for bounded proportions. Bound the
tails deliberately, and say in a comment what the choice believes about the
world. See ADR 0008, which also explains why the generator shapes its prior
rather than sampling and rejecting.

## Generated level geometry

`Content/Maps/L_Hauler.umap` is **generated, not hand-edited**. The source of
truth is `Tools/hauler_layout.py`: a floor plan of rooms, doors, windows and
seals, plus furniture placements. `Tools/floorplan.py` derives the walls,
`Tools/props.py` holds the furniture templates, `Tools/placement.py` resolves
everything room-relative into world space, and `Tools/build_hauler.py`
realises it in the editor. Actors the script owns are prefixed `hauler_` and
are rebuilt on every run, so hand-placed changes to them are lost. Materials
are rebuilt too: tune them in `build_hauler.py`, not in the editor.

```bash
python3 Tools/test_floorplan.py && python3 Tools/test_placement.py
python3 Tools/validate_hauler.py        # no editor needed, ~1s
~/UnrealEngine/UE_5.8/Engine/Binaries/Linux/UnrealEditor-Cmd \
    "$PWD/DeepSpace.uproject" \
    -run=pythonscript -script="$PWD/Tools/build_hauler.py" \
    -unattended -nopause -nosplash -NoLiveCoding
```

`validate_hauler.py` voxelises the ship at 10 cm and checks soundness — the
plan is self-consistent, the hull is sealed, everything is one connected piece
— and **intent**: every region is reachable in its posture by a capsule with
width, the crawlway can *only* be entered crouching, the corridor keeps a 12 m
clear run for sliding, and no furniture blocks a door or the console. A ship
can be perfectly playable and still fail on intent; that is the point.
Reachability is bounded to each room's floor, because roofs are standable and
connected — the milestone 1 validator could reach rooms across the roof.

`Tools/verify_level.py` then measures the *built* actors and compares them to
the layout. Keep both: the validator checks the layout is sound, the verifier
checks the level matches it. An early build placed every box half its own size
off because `SM_Cube`'s pivot is at its minimum corner rather than its centre,
and the layout validated perfectly throughout — only measuring built actors
catches that. **Meshes do not agree on pivot placement** — `SM_Cube` is corner-origin,
`SM_ChamferCube` centre-origin, `SM_Cylinder` base-centre, and the engine
`Sphere` used for stars is centre-origin — so never assume one; read the
bounding box. `build_hauler.py` scales and offsets every mesh from its
measured bounds for exactly this reason.

`unreal.log` output does not reach stdout under the commandlet; these scripts
write to `Saved/hauler_build.txt` and `Saved/verify_level.txt`.

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
- **The movement contract lives in `Tools/movement_contract.json`**, read by
  the layout and by the `DeepSpace.Player.MovementContract` test. Change it
  there; each side fails its own test if the other no longer fits.
- `ABP_DeepSpaceBody` is hand-wired and may only wire (ADR 0002); the import
  script updates its blend spaces in place, never deleting them, so its
  references survive a re-import.

### The camera is not attached to the head

It looks like it is, but `ADeepSpaceCharacter::PlaceCamera` positions it every
frame instead, because **the animation does not know about the ship**. Attached
to the `head` bone it goes wherever the clip puts it: the retargeted crouch
idle carries the head **57 cm** from the capsule's axis against a **34 cm**
capsule, so crouching against a wall put the view outside the hull.

`PlaceCamera` follows the head exactly sideways, damps only the vertical (bob
is vertical; a horizontal lag lets the body lean into frame), applies the
forward eye offset in the view's **yaw only** (swung with pitch it dives into
the neck when you look down), and sweeps a sphere out from the capsule's axis
so the eyes stop at a wall with more than the 10 cm near plane to spare.

Two guards, and they check different things:

- `check_anim_heights.py` checks every clip's peak head height against its
  posture's capsule, so a taller clip fails there rather than in play.
- `DeepSpace.Player.CameraStaysInsideWalls` and `.CameraDoesNotDiveWhenLookingDown`
  check the placement itself.

Sideways reach is deliberately *not* guarded per clip: heads do leave the
capsule, and the sweep is what handles it.

### Changing the character's C++ components invalidates its Blueprint

`BP_DeepSpaceCharacter` stores a template for every native component it
inherits, holding the values from when it was last saved. Add, remove or
rename a component in C++ and the asset is stale — and the symptom can hide.
Removing `CameraArm` left the saved camera template carrying the old
`bUsePawnControlRotation = false`, so the *first* editor session after the
change could not look around; the next one, reinstanced, was fine.

After any such change, recompile and save the Blueprint, then check:

```bash
strings -a Content/Blueprints/BP_DeepSpaceCharacter.uasset | grep -i CameraArm
```

See ADR 0002's second amendment.
