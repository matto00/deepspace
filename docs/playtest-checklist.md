# DeepSpace Playtest Checklist — Milestone 1

Run before declaring milestone 1 complete. Manual; there is no automated
substitute for embodied behavior.

**Record actual results, including failures.** A failed item is information,
not something to quietly fix and re-declare passing — note it, fix it, and
re-run the affected section.

## Movement and collision
- [x] Spawn in the bunk, standing, camera at plausible eye height
- [x] Walk every room: bunk, corridor, cockpit, galley, engineering, airlock, cargo bay
- [x] The crawlway is too low to enter standing (crouch arrives in the movement sub-project)
- [x] No falling through floors anywhere, including across every door threshold
- [x] No getting stuck on doorframes, wall seams, or furniture
- [x] Cannot walk through walls, furniture, or the airlock's outer door
- [x] Cannot escape the ship interior
- [x] Jump does not clip through any ceiling

## Camera
- [x] Mouse look is smooth, correct direction on both axes
- [x] Looking straight up and straight down does not flip or invert
- [x] Camera never clips inside geometry while walking

## View
- [x] Cockpit window shows stars
- [x] Galley viewport shows stars
- [x] The cargo bay reads as two storeys tall

## Interaction
- [x] Approaching the engineering console within reach shows a prompt
- [x] Prompt disappears when looking away, and when too far
- [x] Pressing E toggles the console
- [x] Powered console reads DRAW 620 W / 1000 W, HEADROOM 380 W
- [x] Console reads OFFLINE on spawn
- [x] No furniture produces a prompt

## Stability
- [x] Play for two minutes continuously without a crash or hitch

## Body and movement
- [x] Looking down shows the body; no view from inside the head or neck
- [x] Looking straight down does not show through the body, or past it behind you
- [x] Walking keeps the arms out of view; sprinting brings them into frame
- [x] Sprinting does not swing the body's lean into frame ahead of the camera
- [x] Hold Left Shift: visibly faster forward; no faster strafing or backing up
- [x] Camera bob while running is comfortable
- [x] C crouches and lowers the view; C again stands
- [x] Crouched hard against a wall, the view stays inside the ship
- [x] Crouched, the crawlway can be entered and crossed, cargo bay to engineering
- [x] Inside the crawlway, C does nothing (no room to stand)
- [x] The view never passes through a ceiling, standing or crouched

## Pilot seat
- [x] Looking at the port cockpit seat shows "Sit in  Pilot Seat"
- [x] E sits: the view settles at seated height, facing the window
- [x] Seated, the view turns about ±100° and ±70° and no further
- [x] Seated, the prompt reads "Stand up", whatever you look at
- [x] E stands you up behind the seat, free to walk
- [x] The starboard seat gives no prompt

## Result — movement, body and pilot seat, 2026-09-22

**PASS.** Played in `L_Hauler`: the body reads as a body, sprint and crouch are
distinct, the crawlway is crouch-only, and the helm seats and releases the
player with the ship's pilot set.

Three failures found on the *way* to this pass, all fixed before it, and all
the same root cause — the camera was attached to the `head` bone, so the
animation decided where it went and nothing checked the result:

- **The body leaned into frame while sprinting.** The spring arm's lag is
  positional in every axis, so under the lean the head ran ahead of the
  trailing camera.
- **Looking down showed through the body, and out behind it.** A spring arm
  applies `SocketOffset` in the *view's* rotation, so pitching down 90° swung
  the 8 cm forward offset into 8 cm downward and put the camera 2 cm below the
  head bone, inside the neck.
- **Crouching against a wall showed the outside of the ship.** Measured: the
  retargeted crouch idle carries the head 57 cm from the capsule's axis,
  against a 34 cm capsule. The spring arm's own collision test would have
  caught this, but the engine skips it when `TargetArmLength == 0`.

`ADeepSpaceCharacter::PlaceCamera` replaced the spring arm;
`DeepSpace.Player.CameraStaysInsideWalls` and
`.CameraDoesNotDiveWhenLookingDown` pin the geometry.

A fourth failure appeared only after the fix, and is the one worth remembering:
removing the native `CameraArm` left `BP_DeepSpaceCharacter` naming a component
its class no longer had, and its saved camera template kept the old
`bUsePawnControlRotation = false`. The view would not turn *at all* on the
first editor session after the change, and was fine on the next — a bug that
appears once and then hides. See ADR 0002's second amendment.

## Result — milestone 1, 2026-09-20

**PASS.** Every item above, played in `L_Hauler`.

Noted, not a checklist failure: the console readout text sits in the centre of
the panel face rather than where intended. It reads correctly — `DRAW 620 W /
1000 W`, `HEADROOM 380 W` — so the interaction items pass. Position is a
`TextRender` placement in `BP_ShipConsole`, being adjusted by hand.

Two failures found on the *way* to this pass, both fixed before it:

- **Geometry was scattered and the Player Start was outside the ship.**
  `SM_Cube`'s pivot is at its minimum corner, so every generated box landed half
  its own size off. See ADR 0004; `Tools/verify_level.py` now catches this class.
- **Mouse look did nothing.** The template puts `Mouse2D` in a separate
  `IMC_MouseLook` context driving `IA_MouseLook`; `IMC_Default` binds look to the
  gamepad right stick only. The character now adds both contexts.

## Result — ship expansion, 2026-09-20

**PASS.** Every item above, played in the generated eight-room `L_Hauler`,
spawning in the bunk. The crawlway's crouch-only property is confirmed only
in the sense that it cannot be entered standing; crouch itself arrives in the
movement sub-project.

