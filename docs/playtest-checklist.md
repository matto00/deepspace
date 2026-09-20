# DeepSpace Playtest Checklist — Milestone 1

Run before declaring milestone 1 complete. Manual; there is no automated
substitute for embodied behavior.

**Record actual results, including failures.** A failed item is information,
not something to quietly fix and re-declare passing — note it, fix it, and
re-run the affected section.

## Movement and collision
- [ ] Spawn in the corridor, standing, camera at plausible eye height
- [ ] Walk the full ship: corridor → cockpit → corridor → engineering → bunk
- [ ] No falling through floors anywhere
- [ ] No getting stuck on doorframes or wall seams
- [ ] Cannot walk through walls
- [ ] Cannot escape the ship interior
- [ ] Jump does not clip through the ceiling

## Camera
- [ ] Mouse look is smooth, correct direction on both axes
- [ ] Looking straight up and straight down does not flip or invert
- [ ] Camera never clips inside geometry while walking

## View
- [ ] Cockpit window shows stars, not void or default sky

## Interaction
- [ ] Approaching the console within reach shows a prompt
- [ ] Prompt disappears when looking away
- [ ] Prompt disappears when standing too far away
- [ ] Pressing E toggles the console
- [ ] Powered console reads DRAW 620 W / 1000 W, HEADROOM 380 W
- [ ] Unpowered console reads OFFLINE
- [ ] Console reads OFFLINE on spawn, before any interaction
- [ ] Verb changes between "Power on" and "Power off"
- [ ] No other surface in the ship produces a prompt

## Stability
- [ ] Play for two minutes continuously without a crash or hitch
