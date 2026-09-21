# DeepSpace — Ship Expansion Design

**Date:** 2026-09-20
**Status:** Draft, awaiting review
**Follows:** Milestone 1 — walk the ship (complete)
**Foundation:** `2026-09-20-deepspace-foundation-design.md`

## Premise

Milestone 1 proved the loop: walk a small hauler, reach a console, read live
ship state. The ship itself is four grey rooms and a bed. This sub-project grows
it into a ship that reads as somewhere to live — larger, furnished, and with a
visual identity — while keeping everything generated, reviewable, and validated.

It is the first of three sub-projects, in this order:

1. **Ship expansion** — this document.
2. **Movement** — sprint, crouch, slide.
3. **Pilot view** — a third-person camera on the ship, which needs an exterior.

The ship is designed *for* sub-project 2: it contains a space that requires
crouching and a straight run built for sliding. Each sub-project gets its own
design, spec, and plan.

## Goals

- Grow the hauler from four rooms to eight, on one deck, with one double-height
  volume for scale contrast.
- Describe the ship as a **floor plan** — rooms, doors, windows — and derive the
  walls, rather than hand-listing boxes.
- Give the ship a coherent **clean retro-future** look through generated
  materials.
- Furnish every room with stand-in props composed from primitives.
- Extend validation to check *intent* as well as soundness: the crawlway must
  require crouching, and the corridor must stay clear for sliding.

## Non-goals

- A ship exterior or hull model. That belongs to sub-project 3.
- Doors that open or close. The airlock's outer door is static and solid.
- Stairs, ladders, or a second deck.
- Custom-modelled art, imported textures, or Fab/Megascans assets.
- Anything the rooms *do* — cargo, cooking, sleeping.
- Procedural generation. The layout remains hand-designed and deterministic.

## Architecture

The existing pipeline is kept, with one new stage in front of it:

```
hauler_layout.py      floor plan: rooms, doors, windows, seals, placements
      │
floorplan.py          rasterise → derive walls → carve openings → merge to boxes
      │               (plus props → parts, placements → world positions)
      ▼
   boxes  ──►  validate_hauler.py    is the design sound, and does it keep its intent?
      │
      ├────►  build_hauler.py        realise in the editor
      │              │
      │              ▼
      └────►  verify_level.py        does the built level match the boxes?
```

`validate_hauler.py`, `build_hauler.py` and `verify_level.py` continue to consume
`(label, centre, size, role)` boxes. They learn about roles and props; they do
not learn about rooms, which stay the generator's concern.

### New and changed files

| File | Change |
|---|---|
| `Tools/floorplan.py` | **New.** `Room`, `Door`, `Window`, `Seal`, the rasteriser, wall derivation, greedy merge, placement resolution. No `unreal` import. |
| `Tools/props.py` | **New.** Prop templates as lists of parts. No `unreal` import. |
| `Tools/test_floorplan.py` | **New.** Plain-`python3` tests for the generator. |
| `Tools/hauler_layout.py` | Rewritten: the floor plan and placements, no raw boxes. |
| `Tools/validate_hauler.py` | Posture-aware reachability, semantic floor-plan checks, keep-clear zones, intent checks. |
| `Tools/build_hauler.py` | Builds from generated boxes; generates materials; assigns roles; spawns props. |
| `Tools/verify_level.py` | Consumes generated boxes and prop parts. Otherwise unchanged. |

## The floor plan model

Rooms are axis-aligned rectangles given by **minimum corner and size**, which
reads like a floor plan. All room coordinates and sizes must be multiples of
10 cm, the grid the rasteriser uses; the validator rejects anything off-grid.

```python
Room(name, x, y, w, d, height)
Door(room_a, room_b, along, width, height)     # an opening in their shared wall
Window(room, side, along, width, sill, head)   # glazed opening in an exterior wall
Seal(room, side, along, width, height)         # a closed door in an exterior wall
```

### Deriving walls

1. Rasterise the plan onto a 10 cm grid. Each room marks its interior cells.
2. **A wall cell is any cell bordering an interior cell that is not itself
   interior.** Two rooms one wall-thickness apart therefore share a single wall,
   and corners and T-junctions need no special handling.
3. A wall cell's height is the height of the **tallest** room it borders, so the
   wall between the corridor (250) and the cargo bay (500) closes the bay fully.
4. Doors and windows carve cells up to their own height, leaving a lintel above
   and, for windows, a sill below. Windows and seals add a glazing or door box
   in the carved space. **Lintels are emitted as their own boxes**, not merged
   into the wall, so they can carry the trim role; this is what gives each
   opening a visible frame head.
5. Floors and ceilings are emitted per room at that room's height.
6. A greedy merge packs remaining wall cells into as few boxes as possible.

This replaces computing wall segments per room edge and de-duplicating them —
the approach behind milestone 1's hull leak and its hand-worked doorway
arithmetic.

## The ship

X runs fore, Y runs starboard, and the floor is at Z = 0. Walls are 10 cm. Every
coordinate below is an interior rectangle.

```
                          STARBOARD (+Y)
  ┌────────────────┐┌─ crawlway ──┐┌──────────────┐┌──────────────┐
  │                ││  h110       ││ ENGINEERING  ││    GALLEY    │  ◇ viewport
  │                │└────────────┘│  ▣ console   ││              │
  │   CARGO BAY    │   (void)      └──────┬───────┘└──────┬───────┘
  │   h500         │                      │               │         ┌─────────┐
  │   2 storeys    ═══  CORRIDOR — 14 m straight, h250  ════════════ COCKPIT │◇
  │                │                                                │         │
  │                │               ┌──────┴──┐    ┌───────┴──────┐  └─────────┘
  │                │               │ AIRLOCK │    │     BUNK     │
  └────────────────┘               └──▓──────┘    └──────────────┘
                          PORT (−Y)
```

| Room | x | y | w | d | h | Purpose |
|---|---|---|---|---|---|---|
| corridor | 0 | −75 | 1400 | 150 | 250 | The spine. Kept clear as the **slide run**. |
| cockpit | 1410 | −200 | 350 | 400 | 250 | Forward window, flight stand-ins. |
| cargo_bay | −810 | −400 | 800 | 900 | 500 | The one big volume. Two storeys. |
| engineering | 400 | 85 | 400 | 400 | 250 | Reactor, workbench, the console. |
| galley | 810 | 85 | 490 | 400 | 250 | Common room, starboard viewport. |
| crawlway | 0 | 395 | 390 | 90 | 110 | Crouch-only shortcut, cargo → engineering. |
| airlock | 100 | −335 | 250 | 250 | 250 | Sealed outer door on the port hull. |
| bunk | 600 | −385 | 400 | 300 | 250 | **Player Start.** |

Openings:

| Joins | Width | Height | Notes |
|---|---|---|---|
| corridor ↔ cockpit | 150 | 220 | |
| corridor ↔ cargo_bay | 250 | 240 | Wide cargo door |
| corridor ↔ engineering | 120 | 220 | |
| corridor ↔ galley | 150 | 220 | |
| corridor ↔ airlock | 120 | 220 | Inner opening |
| corridor ↔ bunk | 120 | 220 | |
| cargo_bay ↔ crawlway | 80 | 100 | Crouch height |
| crawlway ↔ engineering | 80 | 100 | Crouch height |
| cockpit, fore | 300 | sill 100, head 180 | Window |
| galley, starboard | 200 | sill 100, head 170 | Viewport |
| airlock, port | 120 | 220 | Seal — closed outer door |

The spaces marked *void* are outside the hull. The Player Start moves from the
corridor to the bunk: the game opens with waking aboard your ship.

## Derived placement

Nothing that sits on or in a room is given as a world coordinate. Every
hand-computed coordinate is one that can silently disagree with the surface it
belongs on — the lesson of milestone 1's pivot bug.

- **Lights** are derived per room on a grid of roughly 3 m spacing, with at
  least one per room at ceiling centre. Small rooms get one; the cargo bay gets a
  grid of several. A new room is lit by default.
- **The console** is placed as *engineering, starboard wall, centred, 120 cm up*.
- **The Player Start** is *bunk, centre*.
- **Props** are placed in room-local coordinates (below).

## Materials

The look is **clean retro-future**: 2001, Outer Wilds, the Normandy. Light
panels, rounded edges, soft even light, one accent colour.

### One master material, many instances

`M_ShipSurface` is generated from Python, with parameters for base colour,
roughness, metallic, panel size, seam width, and an optional emissive strip.
Every surface is a material instance of it. Retuning the look is editing
numbers, not rebuilding graphs.

### World-aligned seams

**Panel seams are computed from world position, not UVs.** The ship is built
from cubes scaled to very different proportions: a 10 cm × 14 m wall and a 1 m
block are the same mesh. A UV-mapped texture stretches with scale, so seams on
the long wall would be fourteen times wider apart. Seams derived from world
position stay a constant size on every surface and line up across neighbouring
boxes. This is what makes stretched cubes read as panelling.

### Roles

The generator assigns a role to every box and prop part from what it is.

| Role | Look |
|---|---|
| wall | Off-white, satin, 120 × 60 cm panels |
| floor | Light warm grey, slightly rougher, 100 cm tiles |
| ceiling | White, with emissive light panels in the grid |
| trim | Lintels over openings, in the accent colour, non-emissive |
| furniture | Pale grey, soft sheen |
| glass | The existing `M_Glass` |
| screen | Dark glass with an emissive tint |
| accent | Teal emissive, for bands and indicators |

**Accent colour: teal**, matching the console's existing cyan readout. It is one
parameter.

Because surfaces name a role and never a material, real textures later are a
change to `M_ShipSurface` alone.

### Lighting

Retro-future wants soft, even light rather than pools. The six guessed point
lights are replaced by derived lights at lower intensity with wider falloff,
neutral-cool white, with the emissive ceiling panels doing much of the work.

## Furniture

### Props as templates

A prop is a named list of parts. Each part is a primitive with a local position,
a **size in centimetres**, and a role:

```python
PROPS["cargo_container"] = [
    Part("chamfer", at=(0, 0, 60), size=(240, 120, 120), role="furniture"),
    Part("cube",    at=(0, 0, 60), size=(244, 124, 12),  role="accent"),
]
Place("cargo_container", room="cargo_bay", at=(150, 200), facing=90)
Place("cargo_container", room="cargo_bay", at=(150, 200), facing=90, level=1)
```

- **Meshes:** `SM_ChamferCube` for the rounded retro-future edge, `SM_Cylinder`,
  and `SM_Cube` where an edge should be crisp. All from `LevelPrototyping`.
- **Sizes, not scales.** The builder reads each mesh's bounding box, so pivot
  placement is handled by construction.
- **Rotation is limited to 90° steps.** Every part stays an axis-aligned box,
  which lets the validator voxelise furniture exactly.
- **`level`** stacks a prop on the one below, to use the cargo bay's height.

### Inventory

| Room | Props |
|---|---|
| cockpit | Two pilot seats; a wraparound console desk with three screens; an overhead panel |
| cargo_bay | Containers in two sizes, some stacked two-high; a wall rack |
| engineering | A tall reactor cylinder with a teal emissive band; pipe runs along the walls; a workbench; the existing console |
| galley | A table with two benches; a counter with cabinets |
| bunk | A bed with frame and mattress; a locker; a small desk |
| airlock | Two suit lockers; a bench |
| corridor, crawlway | Kept clear — thin wall-mounted conduit only |

The pilot seats are the natural hook for sub-project 3 but remain decorative.
Only the console is interactable.

## Validation

### Floor-plan semantics

Checked before any geometry is derived:

- rooms do not overlap, and every coordinate is on the 10 cm grid
- every door joins two rooms that share a wall, lies within that shared wall, and
  is no taller than either room
- windows and seals lie on exterior walls
- every placement names a real room and prop, and its footprint lies inside the
  room

### Geometry

Unchanged in purpose, now run against derived walls **and furniture**:

- the hull is sealed
- all geometry forms one connected component
- the console is not buried

### Posture-aware reachability

Walkability is computed at two clearances, **standing 180 cm** and **crouched
90 cm**. Each region declares the posture it needs:

- every region is reachable in its declared posture from the Player Start
- the crawlway is reachable crouched **and unreachable standing**

The second assertion protects intent. A crawlway raised to 190 cm leaves a
playable ship but removes the reason to crouch; the validator should say so.

### Keep-clear and intent

- each door has a keep-clear zone 100 cm deep on both sides; no prop may enter it
- the console has a keep-clear zone in front of it
- **the corridor keeps a clear straight run of at least 12 m** — the slide run

### Testing the generator

`Tools/test_floorplan.py`, plain `python3`, tests the parts most likely to go
wrong, at their source:

- two rooms sharing a wall produce one wall, not two
- an L-shaped corner does not leak
- a door carves exactly its width and height and leaves a lintel
- greedy merging exactly tiles the wall cells: no gaps, no overlaps
- a door between rooms that share no wall is rejected

`verify_level.py` continues to compare the built level with the generated boxes.

## The movement contract

The ship now encodes two numbers the character must honour:

- **standing clearance: 180 cm**
- **crouched clearance: 90 cm**

They live as constants in `hauler_layout.py`. Sub-project 2 must keep the
crouched capsule under 90 cm and under the crawlway's 100 cm doors, or the
crawlway becomes impassable while the validator reports the ship as fine. This is
a contract across Python and C++. Sub-project 2 should check it mechanically
where it can rather than rely on this document.

## Risks

- **Material graphs from Python.** World-aligned seams need a handful of
  expression nodes (world position, divide, frac, step) wired correctly. This is
  the most fiddly part of the build. `Content/LevelPrototyping` already contains
  `M_PrototypeGrid` and `MF_ProcGrid`, a world-aligned procedural grid; if
  authoring seams proves brittle, instancing that is the fallback.
- **Blueprint graphs stay manual.** Milestone 1 showed the subobject API is
  fragile. Generation stops at spawning actors and setting properties; it does
  not edit Blueprint graphs.
- **Actor count.** Roughly 80 wall and floor boxes, 150 prop parts, 160 stars.
  All static, and well within budget, but a reason to keep the merge step.
- **Clean surfaces expose stand-ins.** Mitigated by chamfered primitives and
  world-aligned seams; accepted as the cost of the chosen look.

## Definition of done

- `python3 Tools/test_floorplan.py` and `python3 Tools/validate_hauler.py` pass.
- The level builds, and `verify_level.py` passes.
- Playing it: spawn in the bunk; walk all eight rooms; reach engineering through
  the crawlway by crouching (once sub-project 2 provides crouch — until then the
  crawlway's crouch-only status is verified by the validator alone); see stars
  through the cockpit window and the galley viewport; read the console.
- The ship reads as a coherent clean retro-future interior, not as grey boxes.
- The milestone 1 playtest checklist is updated for the new layout and passes.
