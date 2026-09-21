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
