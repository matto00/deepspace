# DeepSpace — Movement, Body, and the Pilot Seat

**Date:** 2026-09-20
**Status:** Approved — plan pending the Mixamo retarget prototype
**Follows:** Ship expansion (complete, playtest passed)
**Foundation:** `2026-09-20-deepspace-foundation-design.md`

## Premise

The ship is now eight furnished rooms, but the player moving through it is a
floating camera: nothing moves the view except the mouse, looking down shows
floor, and walking and sprinting would feel identical. This sub-project gives
the player a body and the verbs the ship was designed for:

- **sprint** and **crouch**, the second of which the crawlway already demands;
- **a visible, animated body** — look down and see it; sprint and see your arms;
- **the pilot seat** — sit at the helm, the first step of pilot mode.

It merges what the expansion spec called sub-project 2 (movement) with the
entry point of sub-project 3 (pilot mode), and adds the body.

## Goals

- Sprint and crouch, with crouch able to enter the crawlway and nowhere else
  requiring it.
- A single full-body mesh the player inhabits, animated by movement, with the
  camera riding its head.
- An interactable pilot seat: sit, look around the cockpit, stand.
- The ship knows when it is piloted.
- The movement contract between ship and character checked mechanically on both
  sides.
- The C++-logic rule extended to Animation Blueprints, and checked mechanically.

## Non-goals

- Slide. The corridor's 12 m slide run stays in the validator as a reserved
  property for when slide arrives.
- Stamina, sprint limits of any kind.
- Directional crouch animation: crouch-walk is forward only.
- Torso lean following look pitch (an aim offset).
- A correct head in the player's shadow.
- The co-pilot seat.
- Footstep or any other sound.
- Anything that happens *while* piloting: flight, ship controls, the exterior
  third-person view. Those are the next sub-project, and grow from the
  `IsPiloted()` seam this one adds.

## Movement

| Verb | Input | Speed | Rules |
|---|---|---|---|
| Walk | WASD | 300 cm/s | — |
| Sprint | hold **Left Shift** | 600 cm/s | Only while moving roughly forward, and not crouched |
| Crouch | **C** toggles | 150 cm/s | Standing up is refused under a low ceiling (engine behaviour) |

- **Sprint eligibility is a pure function** — `CanSprint(bIsCrouched, MoveInput)`
  — so it is unit-testable without a world. "Roughly forward" means the move
  input's forward component dominates and is positive.
- **Crouch is a toggle**, not a hold: the crawlway is four metres long.
- Crouched capsule: **half-height 65 cm** (130 cm tall). Radius unchanged at the
  engine default **34 cm**. Sized from the measured crouch-walk clip — see
  *Amendments* below.
- New input actions `IA_Sprint` and `IA_Crouch` (digital), bound in
  `IMC_Default`, assigned in `BP_DeepSpaceCharacter`.

## The movement contract, checked on both sides

The ship and the character share three numbers: **standing clearance 180 cm**,
**crouched clearance 140 cm**, **capsule radius 34 cm**. Today they are Python
constants in `hauler_layout.py` and prose in the expansion spec. They move into
one file, **`Tools/movement_contract.json`**:

- `hauler_layout.py` loads its constants from it instead of hard-coding them.
- A C++ automation test, **`DeepSpace.Player.MovementContract`**, loads it and
  asserts that the character's default standing capsule height is below the
  standing clearance, its crouched height below the crouched clearance, and its
  radius equal to the contract radius.

Either side changing alone now fails a test on that side.

## The visible body

**One full-body mesh, inhabited.** `BP_DeepSpaceCharacter` assigns the
template's `SKM_Manny_Simple` as the character's mesh — an asset assignment,
within the Blueprint rule. There is no separate arms mesh: the body the player
sees looking down is the same body that swings its arms into view when running.
Walking keeps the arms low and out of frame; the run animation pumps them
forward. That difference is how sprint reads visually, with no dedicated code.

**The camera rides the head.** The camera is attached at the head bone rather
than at a fixed height on the capsule. It follows the head's **position** —
bob, crouch drop, seated height all come from the animation — but not its
**rotation**, which stays entirely under the mouse
(`bUsePawnControlRotation`). A **camera lag** value, tunable in the Blueprint,
smooths the bob; running animations bob hard enough to be uncomfortable
unsmoothed.

**The head is hidden from the player's own view**, so the camera does not look
out from inside the skull. Known stand-in: this also hides it in the player's
shadow. A shadow-only copy of the body fixes it later.

## Animation

**C++ decides; the graph wires.**

- **`UDeepSpaceAnimInstance`** (C++) computes, every frame, `Speed` (ground
  speed) and `Posture` — an enum `EPosture { Standing, Crouched, Seated,
  Falling }` read from the character.
- **`ABP_DeepSpaceBody`** subclasses it. Its AnimGraph is one *Blend Poses by
  EPosture* node feeding the output pose, choosing between:
  - `Standing` → **`BS_DS_Locomotion`**, a 1D blend space on speed:
    idle (0) → walk (300) → run (600)
  - `Crouched` → **`BS_DS_Crouch`**: crouch idle (0) → crouch walk (150)
  - `Seated` → the sitting loop
  - `Falling` → the template's `MM_Fall_Loop`
  with a 0.25 s blend between postures. **No state machine. Nothing in the
  Event Graph.**

### The rule, extended

ADR 0002 is amended to define what an Animation Blueprint may contain:
blend-space players, sequence players, *Blend Poses by* nodes, and the output
pose. Nothing that decides. The drift signal becomes concrete — a state machine
or Event Graph logic appearing in `ABP_DeepSpaceBody` — and is checked by
`Tools/check_anim_blueprints.py`, which reads each Animation Blueprint asset
and fails if it contains either.

## Animation sources

The project has idle, walk and jog on the mannequin skeleton, but no crouch and
no sitting. Those come from **Mixamo** as FBX, downloaded by the developer to
**`SourceArt/Mixamo/`** and versioned through the existing Git LFS rule for
`*.fbx`:

| Clip | Needed for | Required |
|---|---|---|
| Y Bot, T-pose, with skin | Source skeleton for retargeting | Yes |
| Crouch Idle | `BS_DS_Crouch` at 0 | Yes |
| Crouched Walking (in place) | `BS_DS_Crouch` at 150 | Yes |
| Sitting Idle | `Seated` | Yes |
| Stand To Sit, Sit To Stand | Getting in and out of the seat | Optional |
| Running or Sprint (in place) | `BS_DS_Locomotion` at 600 | Optional — the template jog is the fallback |

**Import is scripted**, like the level. `Tools/import_animations.py` imports
the FBX files, sets up a retarget from the Mixamo skeleton onto the mannequin,
batch-retargets every clip, and builds both blend spaces. It is idempotent.

Retargeting from Python is the least certain step in this design. It is
prototyped headlessly against the downloaded files before the implementation
plan is written; if it proves brittle, the plan replaces it with a one-time
guided retarget in the editor and says so.

## The pilot seat

**`APilotSeat`**, a small C++ actor in the shape of `AShipConsole`:

- a collision box enveloping the seat, blocking the Visibility channel, so the
  reach trace hits the seat rather than the decorative prop beneath it;
- an `UInteractableComponent`: *"Sit in — Pilot Seat"*;
- a **seat anchor** — where the character sits and which way it faces;
- an **exit point**, 50 cm behind the seat.

It is placed over the **port** pilot seat by the level builder, from a layout
entry, and verified by `verify_level.py` like the console. The starboard seat
stays decorative.

**Sitting:** posture becomes `Seated`; movement mode is disabled; capsule
collision is disabled so the character can overlap the chair; the character is
placed at the seat anchor facing the window. Look is limited to **±100° yaw**
and **±70° pitch** around the seat's facing.

**Standing:** **E** stands up whatever the player is looking at — while seated
the interaction prompt reads *"Stand up"*. The character moves to the exit
point, and movement and collision are restored.

**The ship knows.** `UShipSubsystem` gains `SetPilot(APawn*)`, `ClearPilot()`
and `IsPiloted()`, called by the seat. Consumers ask the subsystem, as for every
other piece of ship state. Nothing reads `IsPiloted()` yet except its test; it
is the seam the flight sub-project builds on.

## Testing

- **`DeepSpace.Player.MovementContract`** — capsule dimensions against
  `movement_contract.json`.
- **`DeepSpace.Player.Sprint`** — `CanSprint` for forward, strafe, backward,
  and crouched input.
- **`DeepSpace.Ship.Piloted`** — `IsPiloted()` false by default, true after
  `SetPilot`, false after `ClearPilot`.
- **`Tools/check_anim_blueprints.py`** — no state machine, no Event Graph logic.
- **`validate_hauler.py`** — unchanged in behaviour, now reading the contract
  from JSON; the crawlway's crouch-only check is now meaningful in play.
- **`verify_level.py`** — also checks the pilot seat's placement.
- **Playtest checklist** — gains: sprint is visibly faster and shows the arms;
  crouch toggles, enters the crawlway, and cannot stand up inside it; looking
  down shows the body; the camera bob is comfortable; sitting at the helm,
  looking around within limits, and standing up.

## Risks

- **Retargeting from Python** — mitigated by prototyping before the plan, with
  a guided editor fallback.
- **Camera on the head bone** can be nauseating, or clip into the body when the
  arms swing up. Mitigated by camera lag and a forward offset from the head,
  both tunable.
- **Every C++ header change needs an editor restart** on Linux (no Live
  Coding). The plan marks each step that needs `./rebuild.sh --force --launch`.
- **The Animation Blueprint is hand-made** — six nodes, wired from
  instructions, and then checked by script.

## Definition of done

- The three automation tests pass headlessly; the anim-blueprint check passes;
  `validate_hauler.py` and `verify_level.py` pass.
- Playing: sprint down the corridor and see your arms; crouch into the crawlway
  and fail to stand inside it; look down and see your body; sit at the helm,
  look out of the window, stand up.
- The movement no longer feels like a floating camera. That is a human
  judgement, and the point of the sub-project.

## Amendments after the retarget prototype

The Mixamo import and retarget was prototyped headlessly against the downloaded
clips before planning, on a scratch folder. It works end to end from Python:
both IK rigs are auto-generated (the retargeter recognises the Mixamo and
mannequin skeletons), chains auto-map, the T-pose-to-A-pose difference is
auto-aligned, and every clip batch-retargets onto `SK_Mannequin` with its length
intact. **The scripted route is confirmed; no guided fallback is needed.**

Measuring the retargeted clips — bone heights sampled across each clip, against
Epic's native idle as a reference — confirmed correct scale and grounded feet,
and changed three things.

**Clips chosen.**

| Role | Clip | Head bone |
|---|---|---|
| Standing run | `running` | 150–161 cm (native walk: 150–158) |
| Crouch idle | `crouching_idle` | 82 cm |
| Crouch walk | `crouch_walk` | **102–118 cm** |
| Seated loop | `sitting_idle` | 119 cm |
| Sit / stand | `stand_to_sit`, `sit_to_stand` | chain exactly into and out of `sitting_idle` at 119 cm |

`seated_idle` was also downloaded but sits 7 cm higher than the transitions
end, so would pop; `sitting_idle` is used. `typing` and the two
`pilot_flips_switches` clips are imported but unused this round — natural
seated "busy at the helm" variations for the flight sub-project.

**The crouch is taller than designed.** The crouch-walk clip is a high sneak,
peaking at 118 cm, well above the 110 cm crawlway ceiling; with the camera on
the head bone, the view would have passed through the ceiling on every step.
The developer chose to keep the clip and size the ship to it:

- **Crouched capsule 130 cm** (half-height 65), up from 88.
- **Crawlway 140 cm tall, with 140 cm doors**, up from 110 and 100.
- **Contract crouched clearance 140 cm**, up from 90. Standing (176 cm capsule)
  still cannot enter, so the crawlway remains crouch-only; validated, including
  a negative control that raising it to 190 cm fails as no longer crouch-only.

Accepted cost: crouch idle (82 cm) and crouch walk (102–118 cm) differ enough
that starting and stopping a crouched walk visibly changes height.

**A new rule: the camera stays inside the capsule.** The engine guarantees the
capsule fits wherever the player is, so a camera that never rises above the
capsule's top cannot see through any ceiling. Checked by the anim-height test
below rather than by clamping.

**Added test: `Tools/check_anim_heights.py`**, run in the editor like
`verify_level.py`: for every clip in each posture, the head bone's peak height
must be below that posture's capsule height (standing 176, crouched 130). This
turns the measurement above into a guard — a future, taller clip fails it.

