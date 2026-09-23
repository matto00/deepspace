# DeepSpace — Flight and the Counter-Frame

**Date:** 2026-09-22
**Status:** Draft — not implemented
**Follows:** Movement, body and the pilot seat (implemented, playtest passed)
**Implements:** `docs/decisions/0005-the-ship-is-the-origin.md`
**Governed by:** `docs/vision.md` — *Flight modes*, *The cruise is when you live
in the ship*

## Premise

The player can walk the hauler and sit at the helm, and `UShipSubsystem`
already knows the ship is piloted. Nothing happens next. The ship does not
move, and there is nothing outside the windows but 160 spheres sitting still.

This sub-project makes the ship fly — in the specific sense ADR 0005 settled.
The ship actor never moves. Its transform is identity, permanently. The ship
has a *universe* position and orientation, which are numbers on the subsystem,
and everything outside the hull is drawn through the inverse of those numbers.
The player at the helm turns the ship and the stars swing past the window; the
player stands up, walks aft, and the floor under them has not moved a
millimetre, because it never does.

ADR 0005 removed most of what is hard about flight in a walkable ship. What is
left is arithmetic, one conversion function, and a tick order. This spec is
short because of that, and it should stay short.

## Goals

- A pure C++ flight state — position, orientation, velocity, angular velocity,
  angular acceleration — integrated headlessly, with no `UWorld` and no
  `UObject`, in the shape ADR 0003 established for `FShipPowerState`.
- The subsystem owns it, steps it, and is the only thing that writes it.
- A counter-frame root actor that carries the inverse rotation, with the
  starfield hanging off it.
- Exactly one function converting a universe position to a world position, used
  by everything outside the hull without exception.
- Flight controls at the helm, at cruise rates, with the mouse still free to
  look out of the window.
- Most of the above covered by headless automation tests.

## Non-goals

These are out of scope and must not be designed in passing:

- **Hyperjump, destinations, navigation, star charts.** Flight here means
  pointing the ship and moving; where it is going is a later sub-project.
- **Planets, landing, the reference-frame handoff.** ADR 0005 names the handoff
  as deferred work that arrives with planetary landing. It arrives with
  planetary landing.
- **Combat manoeuvring.** `docs/vision.md` makes combat rates *a later ship
  upgrade*, deliberately, so that the early game never produces violent motion
  and the thrown-bodies problem can be solved later against a ship that has
  earned it. Nothing in this spec assumes a rate higher than cruise, and the
  limits are data so that the upgrade is a different set of numbers rather than
  a different model.
- **Bodies thrown around the interior.** Follows from the above. The flight
  state *exposes* angular acceleration because that is the number fictitious
  forces are computed from, and it costs nothing to expose it now. Nothing
  consumes it in this sub-project.
- **Engine power draw.** The engines will eventually draw from
  `FShipPowerState` and a browned-out ship will fly badly. Not now; the two
  states sit side by side in the subsystem and do not talk yet.
- **A flight HUD, instruments, or an exterior camera.**

### And no character work at all

This is the surprising one, so it is stated explicitly.

**`ADeepSpaceCharacter`, `UCharacterMovementComponent`, the capsule, the camera
and the animation graph are not touched by this sub-project**, beyond binding
new input actions and forwarding them. There is no ride-along, no movement
base, no carried velocity, no gravity redirection, no re-derivation of control
rotation.

That is the entire point of ADR 0005, and it is worth saying plainly because
every other space game with a walkable ship spends weeks here. The interior is
static geometry in a world whose origin never moves. A player standing in the
galley while the ship rolls through 180° is standing on a floor that is not
rotating, under gravity that still points along −Z, with a camera whose
world-frame assumptions (ADR 0005, Consequences) remain exactly as valid as
they were when the ship was parked. The roll is entirely in what they can see
through the window.

The cost of that is recorded in ADR 0005 and is not re-litigated here: this
holds only while there is exactly one walkable interior, and boarding another
ship under way would overturn it.

## The flight state

**`FShipFlightState`**, in `Source/DeepSpace/Ship/ShipFlightState.h/.cpp`,
beside `FShipPowerState` and built to the same rule: a plain struct, no
`UObject`, no `UWorld`, nothing from Unreal but maths types and containers. It
is the complexity sink for flight, so it is the layer that must be trivially
testable, and it is.

Two supporting value types come first.

```cpp
/** What the ship can do. Cruise today; a combat upgrade is a different set
 *  of numbers, not a different model. */
struct DEEPSPACE_API FShipFlightLimits
{
    /** Top speed under cruise assist, cm/s. */
    double MaxSpeed = 20000.0;

    /** How hard the ship changes velocity, cm/s^2. */
    double LinearAcceleration = 4000.0;

    /** Peak turn rate, radians/s, per body axis: pitch, yaw, roll. */
    FVector MaxAngularRate = FVector(0.20, 0.20, 0.30);

    /** How hard the ship changes turn rate, radians/s^2, per body axis. */
    FVector AngularAcceleration = FVector(0.25, 0.25, 0.40);

    static FShipFlightLimits Cruise();
};

/** The pilot's intent, normalised. Every field is clamped to its range on
 *  the way in, so a bad caller cannot exceed the limits. */
struct DEEPSPACE_API FShipFlightCommand
{
    /** Fraction of MaxSpeed to hold, -1..1. Persistent: set and leave. */
    double Throttle = 0.0;

    /** Fraction of MaxAngularRate on each body axis, -1..1:
     *  X = pitch, Y = yaw, Z = roll. Held, not persistent. */
    FVector AttitudeRate = FVector::ZeroVector;
};
```

And the state itself:

```cpp
/**
 * Where the ship is in the universe, which way it points, and how it is
 * moving. The ship actor's transform is identity permanently (ADR 0005);
 * this struct holds everything that would otherwise be in it.
 *
 * Pure arithmetic: no UObject, no UWorld, unit-testable with no world at all,
 * exactly as FShipPowerState is.
 */
struct DEEPSPACE_API FShipFlightState
{
public:
    void SetLimits(const FShipFlightLimits& NewLimits);
    const FShipFlightLimits& GetLimits() const;

    /** Clamped on the way in. */
    void SetCommand(const FShipFlightCommand& NewCommand);
    const FShipFlightCommand& GetCommand() const;

    /** Zero the attitude command, leaving throttle alone. Called when the
     *  pilot leaves the seat: a ship nobody is flying does not keep turning. */
    void ReleaseAttitude();

    /** Advance by DeltaSeconds. Internally fixed-step; leftover time is
     *  carried to the next call, so the result depends on elapsed time and
     *  not on how it was chopped into frames. */
    void Step(double DeltaSeconds);

    FVector GetUniversePosition() const;
    FQuat   GetUniverseOrientation() const;
    FVector GetVelocity() const;              // universe frame, cm/s
    FVector GetAngularVelocity() const;       // body frame, rad/s
    FVector GetAngularAcceleration() const;   // body frame, rad/s^2, last step
    FVector GetLinearAcceleration() const;    // universe frame, cm/s^2
    double  GetSpeed() const;

    /** Ship -> universe. The ship's transform if the ship moved. */
    FTransform GetUniverseTransform() const;

    /** Universe -> ship. What the counter-frame draws through. */
    FTransform GetCounterFrameTransform() const;

    /**
     * THE conversion. Universe position -> Unreal world position.
     * Everything outside the hull is placed through this and nothing else.
     */
    FVector UniverseToWorld(const FVector& UniversePosition) const;
    FVector WorldToUniverse(const FVector& WorldPosition) const;

    /** Direction only: for things at effectively infinite distance. */
    FVector UniverseDirectionToWorld(const FVector& UniverseDirection) const;

    /** Placing the ship without flying there. Used by level setup and tests. */
    void SetUniverseTransform(const FVector& Position, const FQuat& Orientation);

private:
    void SubStep(double FixedDelta);

    FVector Position = FVector::ZeroVector;        // cm, universe frame
    FQuat   Orientation = FQuat::Identity;
    FVector Velocity = FVector::ZeroVector;        // cm/s, universe frame
    FVector AngularVelocity = FVector::ZeroVector; // rad/s, body frame
    FVector LastAngularAcceleration = FVector::ZeroVector;
    FVector LastLinearAcceleration = FVector::ZeroVector;

    FShipFlightLimits Limits = FShipFlightLimits::Cruise();
    FShipFlightCommand Command;
    double Accumulator = 0.0;

    static constexpr double FixedStep = 1.0 / 120.0;
    static constexpr int32 MaxSubStepsPerCall = 8;   // spiral-of-death guard
};
```

**The model, in one paragraph.** Cruise is Newtonian integration with an assist
on top. Each substep, the target angular velocity is
`Command.AttitudeRate * Limits.MaxAngularRate`; the actual angular velocity is
moved toward it at no more than `Limits.AngularAcceleration`, and the
difference divided by the substep *is* the angular acceleration that gets
recorded. Orientation is then advanced by the quaternion exponential of
`AngularVelocity * dt` expressed in body axes and renormalised. Target velocity
is `Throttle * MaxSpeed` along the ship's forward vector *in the universe
frame*, and actual velocity is moved toward it at no more than
`LinearAcceleration`. Position integrates from velocity.

Three things about that are deliberate and each is a decision below: the
integration is fixed-step; the assist means there is no drift when the player
lets go, which is not how vacuum works and is exactly what cruise wants; and
the ship's own acceleration is a first-class output because that is what
throwing bodies around will read.

**Angular velocity lives in body axes** — pitch, yaw, roll are things the pilot
does to the ship, not to the universe, and a body-frame rate is what a rate
limit means physically. The conversion to universe axes happens once, inside
the orientation integration.

## Where it lives, and who may write it

`UShipSubsystem` gains an `FShipFlightState FlightState;` member beside
`PowerState`. Consumers ask; nothing stores a copy. That discipline is ADR
0003's and it applies here unchanged.

Two Unreal-specific changes:

**The subsystem starts ticking.** A `UWorldSubsystem` does not tick — it is
just an object with the world's lifetime. To get a per-frame callback it must
derive from `UTickableWorldSubsystem` instead, which mixes in
`FTickableGameObject` and requires three overrides: `Tick(float DeltaTime)`,
`GetStatId()` (one line, via the `RETURN_QUICK_DECLARE_CYCLE_STAT` macro) and
`IsTickable()`. `UShipSubsystem` changes base class. `Tick` does one thing:
`FlightState.Step(DeltaTime)`. Tickable subsystems run early in the world tick,
before actors, which is what the counter-frame needs.

**Writing is gated on being the pilot.** The seat already reports who is flying
through `SetPilot`/`ClearPilot`, and that is the authority seam. The subsystem
gains:

```cpp
/** Ignored unless Commander is the current pilot. Returns false if refused. */
UFUNCTION(BlueprintCallable, Category = "Flight")
bool SetFlightCommand(APawn* Commander, float Throttle, FVector AttitudeRate);

UFUNCTION(BlueprintPure, Category = "Flight")
FVector GetShipVelocity() const;

UFUNCTION(BlueprintPure, Category = "Flight")
float GetShipSpeed() const;

UFUNCTION(BlueprintPure, Category = "Flight")
FTransform GetCounterFrameTransform() const;

/** The one conversion, exposed. */
UFUNCTION(BlueprintPure, Category = "Flight")
FVector UniverseToWorld(FVector UniversePosition) const;

const FShipFlightState& GetFlightState() const;   // C++ only, read-only
```

`ClearPilot` gains one line: `FlightState.ReleaseAttitude()`. Stand up
mid-turn and the ship stops turning, over a second or so at cruise
deceleration. Throttle is not released — it is a cruise setting, and the whole
point of the vision's *"the cruise is when you live in the ship"* is that the
player sets a heading and a speed and then goes and lives.

Note there is no non-const public accessor for the flight state. The only write
paths are `SetFlightCommand` (gated on the pilot), `ClearPilot`, and the
subsystem's own tick. Nothing else can move the ship, which is what makes the
state trustworthy.

## The counter-frame

**`AShipCounterFrame`**, in `Source/DeepSpace/Ship/ShipCounterFrame.h/.cpp`: an
actor whose root is a bare `USceneComponent` and whose entire job is to be the
parent of everything outside the hull.

Each frame, in `Tick`:

```cpp
SetActorRotation(Ship->GetCounterFrameTransform().GetRotation());
```

Rotation only. **The counter-frame's translation is always zero, and this is
load-bearing.** The naive reading of "transform by the inverse" is to set the
root's location to `-Position` as well, but the ship's universe position will
be measured in billions of centimetres, and an actor sitting at −10⁹ cm with
children hanging off it puts every child's render transform through float
precision that cannot resolve a metre. Instead, translation is applied
*per object* by the conversion function: a child's relative location is
`UniverseToWorld` of its universe position, which subtracts the ship's position
*before* rotating, in double precision, and yields a small number. Objects at
effectively infinite distance — the stars — skip even that, because
subtracting a finite ship position from an infinite distance changes nothing;
they are placed once by direction and simply rotate.

`PrimaryActorTick.TickGroup = TG_PostUpdateWork`, so it runs after everything
that could move the ship this frame and immediately before rendering. (Unreal
splits each frame into tick groups; `TG_PostUpdateWork` is the last one before
the render thread is handed the scene.) Getting this wrong costs one frame of
latency between the ship turning and the view showing it, which at cruise rates
is invisible and would therefore go unnoticed for months — hence saying it
here.

The actor is placed in the level by `build_hauler.py` at the world origin,
labelled `hauler_counterframe`, and `verify_level.py` checks it exists and sits
at identity.

### The starfield

Today `scatter_stars()` spawns 160 `AStaticMeshActor`s on a golden-angle spiral
at a 12,000 cm radius, each an engine `Sphere` scaled to 0.3 with an emissive
material. ADR 0005 notes these become children of the counter-frame and
"should probably become a skybox regardless".

**Decision: not a skybox. One `UInstancedStaticMeshComponent` on
`AShipCounterFrame`, populated in C++ at `BeginPlay` from the same golden-angle
spiral.** Reasoning:

- The stars are *already code* — a deterministic spiral, not authored art. A
  skybox would mean a cubemap texture or a procedural sky material, and the
  developer has no art pipeline. Moving generated geometry into an authored
  asset trades something that works for something that needs skills the project
  does not have.
- Instancing collapses 160 actors and 160 draw calls into one component and one
  draw call, which was the actual complaint. An instanced static mesh component
  holds N transforms for one mesh and renders them in a single pass.
- A dome of real instances can later be given depth — stars at different
  distances, a few bright near objects with real parallax — which a skybox
  cannot. The vision wants a universe with things in it; painting the sky is a
  door that closes.
- It removes 160 actors from a binary `.umap` that goes through Git LFS.

The mesh and material are assigned in a thin Blueprint, `BP_ShipCounterFrame`,
exactly as `BP_ShipConsole` assigns its mesh — asset assignment only, per ADR
0002. Count and radius become `UPROPERTY(EditDefaultsOnly)` tunables on the C++
actor, defaulting to today's 160 and 12,000 cm.

**`build_hauler.py` loses `scatter_stars()` entirely**, along with `STAR_COUNT`
and `STAR_RADIUS`, and gains one spawn of `BP_ShipCounterFrame`. The star
material asset it creates stays — the Blueprint references it. The build
summary's star count goes away and `verify_level.py`'s star check becomes a
counter-frame check. This is a net deletion from the build script, which is the
right direction: the stars stop being level data and become runtime state
belonging to the ship's frame, which is what they always were.

## Flight at the helm

The player sits, the seat sets the pilot, and `IsPiloted()` becomes true. That
seam already exists and is unchanged.

**The mouse always looks. The ship is flown with keys.** This is a real fork and
it is settled here: it would be conventional to fly with the mouse, and it is
wrong for this game. The seat already clamps the view to ±100° yaw and ±70°
pitch precisely so the pilot can look around the cockpit and out of the window,
and the window is where flight is legible at all. Stealing the mouse to steer
would mean the view is always locked to the ship's nose, which is the one thing
the seat was built not to do. Cruise rates are gentle enough that discrete keys
are adequate; a gamepad stick can be bound to the same action later without
changing anything below.

| Control | Input | Effect |
|---|---|---|
| Pitch | **W / S** | `AttitudeRate.X`, −1..1, held |
| Yaw | **A / D** | `AttitudeRate.Y`, −1..1, held |
| Roll | **Q / Z** | `AttitudeRate.Z`, −1..1, held |
| Throttle | **Left Shift / Left Ctrl** | steps `Throttle` up and down; persists |
| Stand up | **E** | unchanged; releases attitude, keeps throttle |

New input actions `IA_Attitude` (Axis2D), `IA_Roll` (Axis1D) and `IA_Throttle`
(Axis1D) are added to `IMC_Default` and bound in
`ADeepSpaceCharacter::SetupPlayerInputComponent` beside the existing ones.
W/A/S/D are reused rather than duplicated: `IA_Move` already early-returns when
seated, so the same keys mean walking on foot and flying in the seat, which is
how the player will expect it.

`ADeepSpaceCharacter` holds the current command as two small members and
forwards on change:

```cpp
void ADeepSpaceCharacter::PushFlightCommand()
{
    if (UShipSubsystem* Ship = UShipSubsystem::Get(this))
    {
        Ship->SetFlightCommand(this, FlightThrottle, FlightAttitude);
    }
}
```

The character does not check whether it is seated. The subsystem checks whether
it is the pilot, which is the same question asked at the only place that can
answer it authoritatively. A character that stands up stops being the pilot and
its next command is refused, which is the correct behaviour arrived at without
a second copy of the rule.

Throttle steps rather than ramping continuously: a discrete setting the player
can read off and return to is what "set a cruise and go and live in the ship"
needs. Eight steps each way is the starting number and is a tunable, not a
constant of the design.

### What the player can actually see

With nothing outside but a star dome at effectively infinite distance,
**rotation is visible and translation is not**. Turning the ship swings the
stars; flying forward at 200 m/s changes nothing on screen at all. This is
physically correct and will read as broken.

This spec does not solve it, because solving it properly means having something
out there to fly toward, which is the navigation sub-project. It is raised as
open question 1 with a cheap interim answer, and the playtest checklist below
asks the question directly rather than letting it pass.

## Testing

Almost all of this is headless, which is the dividend ADR 0005 paid out. New
tests in `Source/DeepSpace/Tests/`, in the style of `ShipPowerStateTest.cpp` —
`IMPLEMENT_SIMPLE_AUTOMATION_TEST`, small scoped blocks, a comment saying what
each block is about.

**`DeepSpace.Ship.FlightState`** — `ShipFlightStateTest.cpp`, no world:

- A fresh state is at the origin, at identity, at rest, and stepping it with no
  command leaves it there.
- Full throttle accelerates at exactly `LinearAcceleration`, reaches `MaxSpeed`,
  and does not exceed it however long it runs.
- Position after *t* seconds at constant velocity is velocity × *t*, to a tight
  epsilon.
- A held yaw command spins up to `MaxAngularRate.Y` and no further; angular
  acceleration is non-zero while spinning up and zero once at rate.
- Integrating a constant yaw rate for exactly 2π/rate seconds returns the
  orientation to identity.
- The orientation quaternion is still normalised after 10,000 substeps of
  simultaneous pitch, yaw and roll. (Quaternion integration drifts off the unit
  sphere; this is the test that catches a missing renormalisation.)
- Commands outside −1..1 are clamped, not honoured.
- `ReleaseAttitude` zeroes the attitude command and leaves throttle.
- **Determinism**: the same two seconds delivered as 240 even steps, as 120
  uneven ones, and as one lump produce the same state within epsilon. This is
  the test that justifies the fixed step and the one that fails first if
  someone "simplifies" it away.

**`DeepSpace.Ship.UniverseFrame`** — the conversion, no world:

- At identity, `UniverseToWorld` is the identity map.
- A point 1,000 m directly ahead in the universe maps to +X × 100,000 in world
  space, before and after the ship is moved along its own forward vector.
- After a 90° yaw, that same universe point maps to the correct world axis.
- `WorldToUniverse(UniverseToWorld(P)) == P` for a spread of points and
  attitudes.
- `GetCounterFrameTransform() * GetUniverseTransform()` is the identity
  transform.
- **Precision**: with the ship at 10¹³ cm (roughly one astronomical unit) from
  the universe origin, two universe points one centimetre apart still map to
  world positions one centimetre apart. This is the test that pins the choice
  of double precision, and the one to look at first if a later change to the
  position type is proposed.

**`DeepSpace.Ship.FlightAuthority`** — needs a world, in the shape of the
existing `ShipPilotTest.cpp`:

- `SetFlightCommand` from a pawn that is not the pilot is refused and changes
  nothing.
- `SetFlightCommand` from the pilot is accepted.
- `ClearPilot` releases the attitude command.

**`Tools/verify_level.py`** — `hauler_counterframe` exists, at world origin, at
identity rotation; no `hauler_star_*` actors remain.

**What needs eyes.** Three things, and only three:

1. **Does a turn read as the ship turning, or as the universe turning?** This
   is the one genuine risk in ADR 0005 and no test can answer it. Sit at the
   helm, hold a yaw, and watch the window.
2. **Are cruise rates gentle?** The numbers above are guesses. Gentle is a
   feeling and the vision is specific about wanting it.
3. **Is the instanced starfield as good as the actor one?** Density, brightness
   and size after the switch, from inside the cockpit and from the galley
   window.

The playtest checklist gains: sit, fly a slow circle, roll through 180°, and
then — the one that matters — **set a throttle, stand up, walk aft to the galley
and watch the stars go past from there**, on a floor that has not moved.

## Definition of done

- The three automation suites pass headlessly; `verify_level.py` passes.
- `build_hauler.py` no longer spawns star actors and the rebuilt level has one
  counter-frame and one instanced star dome.
- No file under `Source/DeepSpace/Player/` has changed except the input
  bindings and the command forward. If character movement code was touched, ADR
  0005 was not followed and something is wrong.
- Flying feels gentle and the interior feels stationary — both human judgements,
  and both the point.

## Decisions needing sign-off

Each of these is a choice a reasonable person could make differently, and each
would cost something real to reverse.

### 1. Universe position is a chunked coordinate, not a flat `FVector`

**Superseded during review — see ADR 0007.** This spec originally specified a
flat double-precision `FVector` in centimetres, on the grounds that Unreal's
Large World Coordinates give doubles for free and resolve sub-millimetre out to
about 1 AU.

That is correct for one solar system and impossible for a galaxy. A double
holds integers exactly to about 9 x 10^15; in centimetres that is roughly 600
AU, which is a comfortable solar system and about seven orders of magnitude
short of a galaxy.

Universe position is therefore an **integer chunk index plus a small local
offset**, per ADR 0007, and the same representation is used at every level down
to planetary surfaces. Two consequences for this sub-project:

- `FShipFlightState` holds that type rather than an `FVector`. Integration
  still happens in the local offset, which is always small, and crossing a
  chunk boundary rebases the offset and steps the index.
- Distances and directions between distant things must be computed through the
  chunk index. Subtracting two local offsets across a boundary is silently
  wrong and looks fine until something is far away. The tests must include a
  cross-boundary case for exactly this reason.

`UniverseToWorld` remains the single conversion and is what keeps this change
contained: nothing outside it knows how a universe position is represented.

### 2. Orientation is `FQuat`, not `FRotator`

**Alternatives:** `FRotator` (Unreal's Euler-angle type, pitch/yaw/roll in
degrees), which is what most Unreal code uses and what the editor displays.

**Chosen because** ADR 0005 identified composing rotations by adding Euler
rotators as one of the four things wrong with the movement-base approach — it
accumulates error off the yaw axis and it gimbal-locks near ±90° pitch. A ship
that can point anywhere, including straight "up", cannot use Euler angles for
its authoritative orientation. Quaternions compose exactly, interpolate
sensibly, and integrate cleanly from an angular velocity.

**Cost to change:** high, and it would be a mistake. Reversing this reintroduces
a class of bug that is hard to see and easy to dismiss as a physics quirk.
`FRotator` is still used at the edges — input, the editor, any debug display —
and converting at those edges is fine.

### 3. Integration is fixed-step (120 Hz) with a carried accumulator, not
per-frame delta

**Alternatives:** integrate once per frame with the frame's delta time, which is
simpler and is what most gameplay code does.

**Chosen because** it makes the flight model a pure function of elapsed time
rather than of frame rate, which is what makes the determinism test above
possible at all, and determinism is what the vision's "generation must be
deterministic and reproducible" instinct will want when the ship's state ever
has to agree between two machines. It also removes a whole category of bug where
flight feels different on a slow frame. The cost is that the rendered state can
lag the true state by up to one substep (8 ms), which at cruise rates is a
fraction of a pixel of star movement.

**Cost to change:** low in code, high in confidence. Dropping to per-frame
integration is a ten-line change; getting back the guarantee it threw away, once
other systems have been built on top of a non-deterministic model, is not.

### 4. Cruise has a flight assist; the ship does not coast freely

**Alternatives:** pure Newtonian — thrust accelerates, nothing decelerates, and
the player counter-thrusts to stop. Or a hybrid where assist can be switched
off.

**Chosen because** the vision's cruise is "gentle", is "most of the game", and
is the mode in which the player gets up and walks away from the controls. A
ship that keeps tumbling because the pilot stood up mid-turn fails that
outright. The assist is implemented as a target the real integration chases,
not as a fake — velocity and angular velocity are still genuinely integrated
and still produce genuine accelerations, so nothing downstream knows or cares.

**Cost to change:** low, and the design anticipates it. Assist-off is a flag
that skips computing the target and lets the command feed acceleration
directly. It is a natural thing for a later ship upgrade to unlock, alongside
combat rates.

### 5. The counter-frame is one actor with a rotation-only transform

**Alternatives:** a component on an existing actor; several counter-frame roots
(one per distance band); applying the full inverse transform including
translation to the root.

**Chosen because** one actor is the simplest thing that is addressable from the
build script, visible in the editor's outliner, and able to be an attach parent
for anything. Rotation-only is the part that matters: applying the translation
to the root would put children at world coordinates where float precision
fails, and would do so silently and only once the ship had flown far enough,
which is the worst possible failure mode. Per-object translation through the
conversion function keeps the arithmetic in doubles until the last moment.

**Cost to change:** low for the actor-versus-component half. **High for the
rotation-only half** — if translation is ever folded into the root, the
precision behaviour of every external object changes at once, and the bug
appears far from the change.

### 6. The starfield becomes one instanced component, generated in C++ at
runtime — not a skybox, and not level data

**Alternatives:** a skybox material or cubemap (ADR 0005's own suggestion); a
single authored star-dome mesh; leaving the 160 actors as children of the
counter-frame.

**Chosen because** the stars are generated code with no art behind them, the
project has no art pipeline, and instancing solves the actual problem (160
actors, 160 draw calls, 160 rows in a binary `.umap`) without closing the door
on giving the sky real depth later. Moving generation from `build_hauler.py`
into the actor also moves it from level data to runtime state, which is where
the vision says generated things belong — the plan is authoritative, the
geometry is derived.

**Cost to change:** low. Nothing depends on the stars; they carry no gameplay.
Swapping the component for a sky material later is a change in one actor and
one Blueprint.

### 7. `UShipSubsystem` becomes tickable, and owns the clock

**Alternatives:** the counter-frame actor ticks and drives the state; a
dedicated flight actor; stepping from the game mode.

**Chosen because** the subsystem owns the state, and whoever owns state should
own its advancement — otherwise the state is only correct when a particular
actor happens to exist, which breaks tests and breaks any level without a
counter-frame. It also keeps the counter-frame a pure view that can be deleted
without the ship stopping.

**Cost to change:** low, but it does mean `UShipSubsystem` changes base class,
which is a header change and therefore a `./rebuild.sh --force --launch` and a
re-check of anything that referenced its type.

### 8. Flight is keyboard-driven; the mouse keeps looking

**Alternatives:** mouse steers the ship while seated, with look on a modifier or
on a free-look key.

**Chosen because** the seat's clamped free look was built deliberately in the
previous sub-project and is how the window becomes the primary flight
instrument. Locking the view to the nose would undo that. Gentle cruise rates
make discrete keys sufficient.

**Cost to change:** low in code — it is a different input binding feeding the
same command. Moderate in design, because mouse-steering probably becomes
attractive the moment combat rates exist, and that is the right time to revisit
it rather than now.

### 9. Flight state is shaped for replication but not replicated

**Alternatives:** wire up replication now (the vision keeps the co-op door
open); or ignore the question entirely and shape the code however is convenient.

**Chosen because** the vision says "no netcode yet" but keeps the door open, and
names exactly one architectural consequence that is cheap now and expensive
later. The shaping that costs nothing today: the authoritative state is one
small copyable struct of plain values; commands travel one way from a
*named pawn* to the subsystem through a single gated function, which is already
the shape of a client-to-server RPC; and nothing outside the subsystem holds a
copy. Actually replicating it — an `FFastArraySerializer`, interpolation,
client prediction — is weeks of work for a feature that does not exist.

**Cost to change:** the shaping makes adding replication later a contained job
rather than a rewrite. Ignoring the shaping would not: state scattered across
actors, or commands applied locally without an authority check, is exactly what
makes retrofitting netcode expensive.

### 10. The pilot is the only writer, and standing up releases attitude but
keeps throttle

**Alternatives:** anyone can command the ship; or the autopilot holds the last
attitude command after the pilot leaves; or standing up cuts throttle too.

**Chosen because** it makes "who is flying the ship" answerable in one place
that already exists, and because the throttle/attitude asymmetry is what makes
the vision's long cruise work — a persistent speed you set and leave, and a
turn that only happens while someone is actually turning.

**Cost to change:** trivial in code, but it is the kind of small rule that
gameplay quietly comes to depend on, so it is worth agreeing on now rather than
discovering later that something assumed the ship keeps turning.

## Open questions

1. **Speed is invisible.** With only a star dome at infinity outside, forward
   motion produces no visual change whatsoever — only rotation reads. The cheap
   interim answer is a sparse field of near-field motes (dust, debris) attached
   to the counter-frame and re-placed through `UniverseToWorld` each frame, or
   wrapped around the ship within a few hundred metres, which gives parallax
   proportional to speed. That is arguably new content rather than flight, and
   it is the kind of thing the vision would want considered on feel rather than
   added reflexively. Decide before implementation whether this sub-project
   includes it, ships without it, or waits for real objects to fly toward.

2. **The numbers.** `MaxSpeed = 200 m/s`, a 0.2 rad/s turn rate and the rest are
   invented. There is nothing in the repo to calibrate against and, per question
   1, nothing to see them against either. They should be treated as placeholders
   set by playtest, and the spec's job is to make sure they live in
   `FShipFlightLimits` where changing them is one edit.

3. **The seed.** The vision says a ship's seed must be authoritative,
   world-level and shareable. No seed exists anywhere in the project yet. The
   starfield spiral is deterministic without one, so this sub-project does not
   force the question, but it is the second place (after level generation) that
   wants a world seed and it will keep coming up.

4. **Where the ship starts.** `SetUniverseTransform` exists so something can
   place the ship, but nothing decides what the universe origin *is* or where
   the hauler sits relative to it. The origin is arbitrary until there are
   destinations, so the default of (0, 0, 0) at identity is fine for now — but
   it is a placeholder, not a decision.

5. **Engines and power.** The flight model does not draw power and an overloaded
   ship flies exactly as well as a healthy one. Deliberate for this sub-project;
   worth confirming that it should stay deliberate rather than being an
   oversight to fix in passing.

6. **Whether the pilot should see anything but the window.** No instruments, no
   speed readout, no attitude indicator. It may be that the window is genuinely
   enough, which would be in keeping with the register; it may be that a ship
   with no readouts feels unfinished rather than austere. A question for the
   playtest, not for the design.
