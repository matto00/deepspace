# DeepSpace Playtest Checklist — Milestone 1

Run before declaring milestone 1 complete. Manual; there is no automated
substitute for embodied behavior.

**Record actual results, including failures.** A failed item is information,
not something to quietly fix and re-declare passing — note it, fix it, and
re-run the affected section.

## Movement and collision
- [x] Spawn in the corridor, standing, camera at plausible eye height
- [x] Walk the full ship: corridor → cockpit → corridor → engineering → bunk
- [x] No falling through floors anywhere
- [x] No getting stuck on doorframes or wall seams
- [x] Cannot walk through walls
- [x] Cannot escape the ship interior
- [x] Jump does not clip through the ceiling

## Camera
- [x] Mouse look is smooth, correct direction on both axes
- [x] Looking straight up and straight down does not flip or invert
- [x] Camera never clips inside geometry while walking

## View
- [x] Cockpit window shows stars, not void or default sky

## Interaction
- [x] Approaching the console within reach shows a prompt
- [x] Prompt disappears when looking away
- [x] Prompt disappears when standing too far away
- [x] Pressing E toggles the console
- [x] Powered console reads DRAW 620 W / 1000 W, HEADROOM 380 W
- [x] Unpowered console reads OFFLINE
- [x] Console reads OFFLINE on spawn, before any interaction
- [x] Verb changes between "Power on" and "Power off"
- [x] No other surface in the ship produces a prompt

## Stability
- [x] Play for two minutes continuously without a crash or hitch

## Result — 2026-09-20

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

