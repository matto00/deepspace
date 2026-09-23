# 0002 — C++ holds the logic; Blueprints only assign assets

**Date:** 2026-09-20
**Status:** Accepted

## Context

Unreal offers two ways to write gameplay: C++ and Blueprints, its visual
scripting system. Blueprints are the path of least resistance in the editor and
most tutorials use them. The developer is new to Unreal, and much of this
project's code is written with an AI collaborator.

## Decision

**All gameplay logic lives in C++. Blueprints subclass C++ classes purely to
assign assets and expose tunable values.**

A Blueprint may set a mesh, pick an input action, fill a colour, or wire a
single presentational node such as copying text onto a `TextRender`. It may not
make decisions.

## Why

- **Blueprints are binary.** A `.uasset` cannot be diffed, merged, or reviewed.
  Logic placed there is invisible to git history.
- **Blueprints are unreadable to Claude.** Everything in `Content/` is opaque to
  the collaborator writing most of the code. Logic there cannot be reasoned
  about, refactored, or checked. In practice this session could only inspect
  Blueprints by grepping `strings` output, which confirmed a parent class or an
  asset reference but could not show how a graph was wired.
- **Blueprint-heavy projects hit performance walls.** Hobby projects that grow
  in Blueprint tend to discover this late and expensively.

## Drift signal

If gameplay logic starts appearing in `Content/`, the plan has been abandoned.
Say so rather than letting it accumulate.

## Consequences

- Blueprint editor work is limited to asset assignment, which the developer can
  do quickly without learning the node graph deeply.
- Some things that would be one Blueprint node become a few lines of C++ plus a
  rebuild. On Linux that rebuild usually means restarting the editor (see
  `CLAUDE.md`), which is the real cost of this choice.
- Where milestone 1 needed data to vary per ship — the starting module loadout —
  it is an `EditDefaultsOnly` property with C++ defaults, not a Level Blueprint.
  The plan originally allowed a Level Blueprint for this as "the one acceptable
  exception"; generating the level (ADR 0004) removed even that.

## Amendment — Animation Blueprints (2026-09-22)

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

## Amendment — the cost of a Blueprint that inherits C++ (2026-09-22)

A Blueprint deriving from a C++ class stores a template for every native
component it inherits, with the property values those components had *when the
Blueprint was last saved*. That makes changing the C++ component list a
two-sided change.

Removing `ADeepSpaceCharacter`'s `CameraArm` left
`BP_DeepSpaceCharacter.uasset` naming a component its class no longer had, and
its `FirstPersonCamera` template still carried the value the old C++ gave it,
`bUsePawnControlRotation = false`. The first editor session after the change
could not look around at all; the next one, having reinstanced the Blueprint
against the new class, was fine. A bug that appears once and then hides is
worse than one that stays.

So: **after adding, removing or renaming a native component, recompile and save
every Blueprint that inherits it**, and confirm the asset no longer names the
old component:

```bash
strings -a Content/Blueprints/BP_DeepSpaceCharacter.uasset | grep -i CameraArm
```

This is a real consequence of the C++-first choice, not an argument against it:
the same rule that keeps logic reviewable means the C++ and the asset must be
kept in step by hand.
