# DeepSpace — Foundation Design

**Date:** 2026-09-20
**Status:** Approved (pending implementation plan)

## Premise

A procedurally generated space exploration game emphasizing the scale of space.
You cruise, choose a destination, hyperjump, arrive. You live aboard a ship you
walk around inside and progressively upgrade.

The long-term arc is: ship interior → flight and navigation → hyperjump → planetary
arrival → planetary surface. This document covers the foundation and the first
milestone only.

## Context

The developer knows general game development concepts but has not worked
substantially inside Unity or Unreal, and has no modeling or art experience.
Target platform for development is Arch Linux (Hyprland, bash, Neovim) on an
RTX 4070 Ti SUPER, 62 GB RAM, 12 cores.

Stated goals: performant eventually, and fairly life-like visually.

## Decisions

### Engine: Unreal Engine 5

Chosen over Godot 4 and Unity 6 for three reasons specific to this project:

1. **Large World Coordinates.** UE5 uses double-precision world coordinates
   engine-wide. For a game whose premise is the vastness of space, this removes
   the single largest technical hazard at zero cost. Unity requires a
   hand-built floating-origin system; Godot requires a custom double-precision
   build.
2. **No art skills.** Fab/Quixel Megascans, MetaHumans, Nanite and Lumen make
   "fairly life-like" reachable without authoring art. This is the decisive
   factor — a walkable ship interior can be kitbashed from scanned assets and
   still look good.
3. **Performance ceiling.** The target hardware is comfortably above UE5's
   requirements, and UE5 is where the headroom is if the project grows.

Accepted costs: heavy editor and shader compilation, Unreal-flavored C++, and
Linux as an unofficially supported platform with possible NVIDIA/Vulkan
friction.

### Code: C++ for systems, Blueprints as thin wrappers

All gameplay logic lives in C++. Blueprints subclass C++ classes only to assign
assets and expose values for live tuning.

Rationale: Blueprints are binary `.uasset` files — not diffable, not mergeable,
and unreadable to an AI collaborator. Logic placed in Blueprints is invisible to
both git history and to Claude. Blueprint-heavy projects are also where hobby
projects commonly hit a performance wall.

**Drift signal:** if gameplay logic starts appearing in `Content/`, the plan has
been abandoned.

### Architecture: system-seeded slice

Build the seams for a data-driven ship from the start, but leave them mostly
empty. Milestone 1 will exercise roughly 20% of the architecture's capability.

Rejected alternatives: a fully hardcoded thin slice (cheapest now, but rewritten
at exactly the point the project gets interesting), and a headless simulation
core first (purest, but weeks before anything is playable and it front-loads
design decisions that require play experience to make well).

**Discipline required:** this approach means building seams, not building the
full data model. If milestone 1 starts designing the complete upgrade system, it
has drifted into the rejected simulation-core approach.

### Working mode: teach as we go

Claude writes code and explains the Unreal-specific concepts behind it. The
developer does the editor work — importing assets, placing geometry, playtesting
— to build genuine engine fluency rather than dependence.

## Toolchain and setup

- **Engine acquisition:** Epic's prebuilt Linux editor binary, not a source
  build. Source builds take hours and tens of GB and are only needed for engine
  modification. Requires linking an Epic account to GitHub in a browser — a
  manual step the developer must perform.
- **Compiler:** Unreal's bundled clang toolchain, pinned explicitly. Using the
  system clang produces confusing link errors.
- **Editor/LSP:** Neovim with clangd, fed by the `compile_commands.json` Unreal
  generates. Preferred over fighting VSCode integration.
- **Version control:** Git with Git LFS for `.uasset` and `.umap` from the first
  commit. Retrofitting LFS after committing hundreds of MB of binary assets is
  painful. `.gitattributes` marks binary types so git never attempts a merge.
  `Binaries/`, `Intermediate/`, `Saved/`, and `DerivedDataCache/` are ignored.

**Open risk, to be verified before anything is built on top:** the current UE5
point release's behavior with this machine's NVIDIA driver version. The hardware
is well supported, but Unreal on Linux with proprietary NVIDIA drivers has
historically had Vulkan-specific quirks. The first task in the implementation
plan is a hands-on verification that the editor launches, compiles C++, and runs
a packaged build.

## Repository layout

```
~/Development/deepspace/
├── CLAUDE.md              # project instructions for Claude
├── docs/
│   ├── superpowers/specs/ # design docs
│   └── decisions/         # short ADRs
├── Source/
│   └── DeepSpace/
│       ├── Core/          # subsystems, game mode, data assets
│       ├── Ship/          # ship state, modules, interactables
│       └── Player/        # pawn, camera, input
├── Content/               # binary assets (LFS): BPs, meshes, materials, levels
├── Config/
└── DeepSpace.uproject
```

`Source/` is the project; `Content/` is its furniture. Everything worth reasoning
about lives in `Source/`.

The module name `DeepSpace` is fixed — renaming an Unreal module after creation
is a manual and error-prone process. The shipping title may differ freely.

## Core architecture

Four components, each with one responsibility.

### `UShipSubsystem` (UWorldSubsystem)

Authoritative ship state: available power, fuel, hull integrity, installed
modules and their condition. Knows nothing about meshes, rooms, or the player.
Everything visible reads from it.

Chosen as a subsystem rather than an actor because it is created and destroyed
with the world automatically, is globally reachable without a singleton, and
cannot accidentally acquire a mesh and a transform and become a god-actor. The
last reason is the operative one.

This is the seam that matters most: later, procedural generation and upgrades
write into this, and the rest of the game reflects it without modification.

### `UInteractableComponent`

Attached to any actor to make it interactable. Holds a display name and a verb
("Open", "Power on"), and broadcasts when triggered.

Deliberately a component rather than an inheritance hierarchy of `ADoor`,
`AConsole`, `ALever`. This is the most reused type in the project; as a component
it composes freely instead of calcifying into a class tree.

### `ADeepSpaceCharacter`

First-person pawn: movement, camera, and an interaction trace that finds
`UInteractableComponent`s ahead of the player and surfaces the prompt.

### `UShipModuleDataAsset`

Data describing a piece of ship equipment: name, power draw, representative mesh.
Milestone 1 defines roughly three, mostly empty. The shape existing is the point.

### Data flow

Walk to the engineering console → the character's trace finds its
`UInteractableComponent` → prompt appears → key press → component broadcasts →
the console's C++ class queries `UShipSubsystem` for current power draw → its
Blueprint updates a screen material.

The console never stores power; it only asks. This is the discipline that keeps
state from scattering across actors.

## Milestone 1 — Walk the ship

### The ship

A small solo hauler: cockpit, corridor, engineering bay, bunk. Walkable end to
end in roughly twenty seconds. Large enough to read as a home, small enough to
finish and to hand-place upgrade points within.

### Definition of done

Launch the game and stand in the corridor in first person. Walk to the cockpit.
Look out the window at stars. Walk to engineering, approach a console, receive an
interaction prompt, trigger it, and see a screen display a real value sourced
from `UShipSubsystem`. Traverse the full ship without falling through geometry or
snagging on it.

### Explicitly out of scope

Flying, hyperjump, planets, procedural generation of any kind, upgrade UI,
saving, sound beyond footsteps, and any custom-modeled art. The interior is
kitbashed from Megascans and starter content. It will look rougher than the
long-term vision; the goal is that the loop exists, not that it looks finished.

## Testing

Unreal's testing story is weaker than a typical application codebase. The plan:

- **Automation Spec tests** for pure C++ — `UShipSubsystem` power math, module
  install and removal. These run headless and are worth writing, because this
  layer accumulates complexity.
- **A manual playtest checklist** for embodied behavior: collision, navigation,
  interaction reach. There is no good substitute, and running these is part of
  the developer's engine-fluency goal.
- **A build script** as a concrete "it compiles and the editor opens" check,
  standing in for CI.

Procedural generation, when it arrives, will need substantially stronger testing
than this. Deterministic seeded generation is highly testable and should be built
that way from its first commit.

## Sequencing beyond milestone 1

Not committed to, recorded for orientation only:

1. Ship systems with real consequence — power routing, fuel, a reason to care
   about the console.
2. Sit at the helm and fly in a local, bounded space.
3. Navigation and hyperjump between destinations; the vastness made legible.
4. Planetary approach and landing.
5. Procedural planetary surfaces.

Each of these gets its own design pass before implementation.
