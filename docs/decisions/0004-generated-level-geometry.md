# 0004 — Level geometry is generated from a script, and validated

**Date:** 2026-09-20
**Status:** Accepted

## Context

The milestone 1 plan called for blocking out the hauler interior by hand in the
editor: 27 boxes placed and scaled one at a time. A level (`.umap`) is a binary
asset. Like a Blueprint, it cannot be diffed or reviewed, and it is opaque to the
AI collaborator. The game is also intended to generate ships procedurally later.

## Decision

The level is **generated**. Three scripts under `Tools/`:

- **`hauler_layout.py`** — the geometry as plain data: boxes, lights, regions,
  console and Player Start positions. No `unreal` import.
- **`build_hauler.py`** — realises the layout inside the editor via the bundled
  Python (`PythonScriptPlugin`). Idempotent: every actor it owns is prefixed
  `hauler_` and is destroyed and rebuilt on each run.
- **`validate_hauler.py`** and **`verify_level.py`** — see below.

The `.umap` is therefore a build output, and `hauler_layout.py` is the source of
truth. Change a number there and re-run rather than nudging actors.

## Why generate

- The layout becomes reviewable text, restoring the property ADR 0002 protects.
- A change is a re-run, not a session of dragging actors.
- It rehearses procedural generation on something small before it matters.

Blender was considered and rejected for the blockout. It would require authoring
collision geometry that a scaled `SM_Cube` provides for free, plus an FBX round
trip for every change, and the developer has no modelling experience. Blender
earns a place later, for props and hull exteriors.

## Validation, in two separate layers

Generated geometry fails in ways that are tedious to find by walking around. Two
checks, deliberately separate:

- **`validate_hauler.py`** asks *is the design sound?* It voxelises the layout
  at 10 cm with no editor, in about a second, and checks that the hull is sealed,
  every named region is reachable on foot with 180 cm of headroom, and the
  geometry is one connected component.
- **`verify_level.py`** asks *does the built level match the design?* It measures
  the actual actors against the layout.

The second exists because of a real bug. `SM_Cube`'s pivot is at its minimum
corner, not its centre, so an early build placed every box half its own size
away from where the layout said — and the Player Start ended up outside the ship.
The layout validated perfectly the whole time, because the layout was fine; the
defect was in turning it into a level. Checking only the input could never have
caught it. That class of bug — generator and data disagreeing while each looks
internally consistent — is characteristic of generated content, which is why the
two layers stay separate.

The validator also caught two genuine design gaps before any playtest: the
cockpit window was an unglazed hole in the hull, and the interior had no lights.

## Consequences

- **Never assume a mesh's pivot.** `SM_Cube` is corner-origin; the engine's
  `Sphere` is centre-origin. The builder reads each mesh's bounding box.
- Hand edits to `hauler_`-prefixed actors are lost on the next build.
- Blueprint graphs cannot reasonably be generated this way (the subobject API
  proved fragile when tried), so generation stops at placing actors and setting
  properties.
- `unreal.log` output does not reach stdout under the commandlet; the scripts
  write their results to `Saved/`.

## Amendment — ship expansion (2026-09-20)

The box list was replaced by a floor plan: rooms, doors, windows, seals.
Walls are derived by rasterising the plan onto the 10 cm grid, where a wall is
any cell bordering an interior that is not itself interior. The generator now
rejects inconsistent plans before any geometry exists — and on its first run
rejected the expansion spec's own room table, whose coordinates were off the
grid the same spec required.

Three meshes, three pivots: `SM_Cube` is corner-origin, `SM_ChamferCube`
centre-origin, `SM_Cylinder` base-centre. With the pivot bug deliberately
reintroduced, the verifier reported 393 failures — every one on an `SM_Cube`
box. The centred furniture passed. A builder that assumed "centred" would have
looked half right.

## Amendment — generation moves into the game (2026-09-22)

This ADR's reasoning stands: generate rather than hand-place, and check it in
two layers. What has changed is *when* generation runs.

ADR 0006 decides that generation is C++ running at game startup, not an editor
commandlet producing a saved `.umap`, and that Python is for prototyping and
testing only. The validator/verifier discipline described above is explicitly
carried forward, with one addition — a generator property test, asserting that
every seed yields a valid plan and that the same seed always yields the same
plan. Read 0006 alongside this one.
