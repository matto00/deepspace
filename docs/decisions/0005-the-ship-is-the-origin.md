# 0005 — The ship is the origin; the universe moves around it

**Date:** 2026-09-22
**Status:** Accepted

## Context

The player walks around inside their ship, and the ship flies. `docs/vision.md`
makes walking during flight a requirement rather than a preference: the long
cruise is not dead time precisely because it is spent on foot, and the player
is never locked out of the interior while the ship is flying.

The obvious implementation — move the ship actor and let the player ride it —
runs into Unreal's movement-base system, which was built for lifts and trains.
Reading the UE 5.8 source turns up four constraints:

- `UpdateBasedMovement` carries a rider's **position** through the base's full
  rotation matrix but its **rotation** through `APawn::FaceRotation`, which
  discards pitch and roll for a first-person pawn; `UpdateBasedRotation` then
  hard-zeroes roll.
- `APawn::IsBasedOnActor` is an exact actor match, not an attachment-chain
  walk, so a multi-actor interior sweeps the player through its own walls. The
  hauler is currently 182 separate `AStaticMeshActor`s.
- `UpdateBasedRotation` composes control rotation by **adding Euler rotators**,
  which is not composing rotations — it accumulates error off the yaw axis.
- `SetGravityDirection` builds its frame with
  `FQuat::FindBetweenNormals(FVector::UpVector, -NewGravityDir)`, which is
  degenerate when the ship's up approaches world-down.

None of these is fatal, and UE 5.8 does more than expected — walkability is
evaluated in gravity space, so pointing gravity along the ship's own down
vector keeps the deck walkable at any attitude, and `PhysicsRotation` will
align the capsule to a rotating gravity vector for you. But taken together they
are two to three weeks of work on the least-exercised paths in the character
movement component, and the control-rotation portion has no clean published
answer anywhere.

## Decision

**The ship actor's transform is identity, permanently. The universe moves
around it.**

The flight model produces velocity, angular velocity and angular acceleration
as plain numbers on the ship subsystem — pure arithmetic with no `UWorld`,
following `FShipPowerState`'s pattern (ADR 0003). Everything outside the hull —
starfield, planets, other objects — hangs off a single counter-frame root that
is transformed by the inverse.

## Why

- **The interior never moves, so no rider problem exists.** No movement base,
  no per-frame carry delta, no transform round-trip drift, no rotation-rate
  limit, and no single-actor refactor of the generated hull. The player walks
  on a floor that is, literally, static.
- **It makes throwing bodies around *easier*, not harder.** Velocity, angular
  velocity and angular acceleration are already known as numbers, so the
  fictitious forces are applied directly rather than fought against a rigid
  carry that is simultaneously gluing the player to the deck.
- **Nothing is thrown away later.** The flight model and the counter-frame are
  consumed unchanged when combat manoeuvring arrives.
- **The vision commits us to a floating origin anyway.** Worlds at Earth's
  scale with no loading screens cannot be done at fixed world coordinates. This
  builds that early instead of retrofitting it.
- **It is where the literature converges** for a single-ship game — an Epic
  staffer's advice for this class of problem is to hold the player's body still
  and rotate everything else; Barotrauma arrived at it independently ("the
  submarine doesn't actually move, only the map").

## What this costs, and the condition that would overturn it

**This decision assumes exactly one walkable interior.** It holds as long as
the only ship whose inside the player occupies is their own — which
`docs/vision.md` states, since company aboard a ship means friends sharing
*your* ship. **Boarding another ship while it is under way would invalidate
this ADR**, because two interiors cannot both be the origin. If that becomes a
goal, this decision must be revisited rather than worked around.

The other cost is deferred rather than avoided: landing on a planet and walking
out needs a reference-frame handoff, where the ship stops being the origin and
the planet starts. That is real work, and it arrives with planetary landing
regardless of what is decided here.

## Consequences

- The ship subsystem gains flight state as pure, headlessly testable
  arithmetic — the same discipline ADR 0003 established for power.
- Every external placement must go through one function that converts a
  universe position into a world position. That single function is also the
  entire switch to a moving-ship architecture, should this ADR ever be
  overturned.
- The 160 star actors `Tools/build_hauler.py` spawns become children of the
  counter-frame root. They are per-level set dressing at a 120 m radius and
  should probably become a skybox regardless.
- Windows show a rotating starfield. This is correct and free.
- `ADeepSpaceCharacter::PlaceCamera` keeps its world-frame assumptions, which
  remain valid. They would **not** survive a moving ship: the eye raise along
  world Z, the world-yaw-only forward offset, and the world-vertical sweep axis
  all assume gravity points along −Z. Recorded here so that overturning this
  ADR does not quietly break the camera.

## Amendment — a fourth write path: the jump (2026-09-25)

The flight spec made the state trustworthy by counting its write paths:
`SetFlightCommand` (gated on the pilot), `ClearPilot`, and the subsystem's own
tick. Nothing else could move the ship. Navigation adds one, and it is
recorded here because this ADR is where "the ship does not move, the universe
does" was decided.

**`FShipFlightState::JumpTo(Arrival)` is the fourth write path.** It is a
translation and nothing else: the universe position changes, and orientation,
velocity and angular velocity are left exactly as they were. It is called
from exactly one place, `UShipSubsystem::StepNavigation`, when the pure
`FShipNavState` reports `Arrived`, inside the tick. The subsystem still hands
out no non-const flight state, so nothing outside it can call `JumpTo`.

The arrival point lies on the line from the departure point to the destination
star, at `max(ds.Nav.StandoffAU × √L, 1.5 × outermost orbit)` from it. So from
where the ship arrives, the star is in exactly the direction it was when the
fold opened, and that direction was inside the alignment cone. The new sun is
where the nose was pointing. Because orientation is untouched, the
counter-frame carries the same inverse rotation before and after, and **the
distant dome does not move**, which is the one thing a jump must not do.

This ADR's shape is what makes that cheap. The ship actor never moved and does
not move now: a jump of several light years is a change to a chunked universe
position (ADR 0007), and everything outside the hull follows through the
counter-frame as it always has. Which system the ship is in then changes by
itself, because `UUniverseSubsystem::GetSystemAt` is asked of the position. No
"arrived at" is recorded anywhere; the sky and the counter-frame rebuild on
`UShipSubsystem::GetJumpSerial()`, a polled cache key.

Two things that look like write paths and are not new ones:

- **The in-system drive** (`SetDriveEngaged`) is a field of the pilot's
  command, `FShipFlightCommand::bDrive`, gated on the pilot exactly as
  `SetFlightCommand` is. The drive's input, the room to the nearest surface,
  is written by the tick.
- **`PlaceShip`** is level setup and tests: the opening placement in
  `UShipSubsystem::OnWorldBeginPlay`, and `ds.Sky.Goto` as a one-shot tuning
  tool. It is not a gameplay path, and no gameplay code calls it.

**Rejected: turning the ship to face the star on arrival**, first with
`FRotationMatrix::MakeFromX` and then with the minimal rotation
`FQuat::FindBetweenNormals(Forward, Dir)`. The first throws away roll, so the
dome could reappear spun by up to 180°. The second keeps roll but still turns
the dome by up to the cone's 8° in one frame, visibly. And neither buys
anything: nothing needs the star centred, and the pilot can centre it.
