# DeepSpace — The Sky: Seeing Space From Inside the Ship

**Date:** 2026-09-25
**Status:** Draft, revised after review — not implemented
**Follows:** Flight and the counter-frame (implemented)
**Builds on:** ADR 0005 (the ship is the origin), ADR 0007 (chunked
coordinates), ADR 0008 (shape the prior)
**Requires first:** procgen step 1 (`GenSeed`, `FGenStream`), see decision 4
**Governed by:** `docs/vision.md`: *Scale is only felt in contrast*, *Approach
takes time, and the time is the content*, *Planets are worlds*

## Context

Today the window shows two things: 160 spheres on a golden-angle shell 120 m
out, and 300 motes wrapped in a cube around the ship. Turning reads, and so
does flying forward, because of the motes. Nothing out there is *somewhere*.
There is no star to be near and no planet to be far from, so there is no scale
at all. The vision is exact about this: scale is only felt in contrast, and a
sky with nothing in it has nothing to contrast with.

This sub-project puts a solar system outside the glass. It adds a background
starfield shaped like a galaxy rather than a spiral, and a local star that is
the brightest thing in the game and lights the deck through the window. It
also adds planets that begin as points of light and resolve into discs as you
close with them, and **a crude in-system drive**, so that closing with them is
something you do at the helm rather than something a console command does to
you. The drive is here because nobody else owns it (decision 8). Everything
else is the *view*. What exists is procgen's to decide. This spec makes sure
that whatever is out there is drawn at its true angle, in its true direction
and in its true depth order.

**The problem is precision, and it is smaller than it looks.** A planet 1 AU
away is 1.5 × 10¹³ cm off. Unreal's Large World Coordinates run out at about
8.8 × 10¹⁵ cm (0.6 AU of headroom in the worst direction). Long before that,
the renderer's float-relative vertex maths, its shadows, Lumen and temporal
anti-aliasing all degrade. You cannot put a planet where it is.

You do not need to. **From inside a ship, the only things a distant body can
show you are its direction and its angular size**, and both survive any
scaling about the eye. This spec is built on that fact. It is also built on
the less convenient fact that the eye is not at the ship's origin: it walks
around a 26 m hull. Decision 1 is sized against that.

### What the player sees: the numbers the design is sized against

At 4K and a 90° horizontal field of view, a pixel is 0.52 mrad (0.03°). With
that:

| Thing | Seen from | Apparent diameter |
|---|---|---|
| A Sun-like star | 1 AU | 18 px, a small blinding disc |
| The same star | 30 AU | 0.6 px, a point, still the brightest in the sky |
| An Earth-sized planet | 1 AU | 0.16 px, a point, if visible at all |
| The same planet | 0.08 AU (12 million km) | 2 px: **the moment it resolves** |
| The same planet | 40,000 km | 18°, fills the cockpit glass |

An approach from 1 AU to a high orbit spans 3.6 decades of distance. For the
first 1.1 of them the planet is a point of light, and for the last 2.5 it is a
world growing in the window. That ratio is not tuned. It falls out of the
geometry, and it is the best thing in this spec. Under decision 8's drive,
the whole approach takes two minutes at full throttle, and the planet is a
point for the first 37 seconds of it.

## Goals

- A background starfield that reads as a galaxy: a band, a brighter bulge,
  a steep brightness distribution, colour from temperature. Generated, seeded,
  one draw call.
- A local star that is a disc when near and a point when far, whose light
  comes through the glass and moves across the deck as the ship turns.
- Planets and moons drawn at their **true** angular size and direction, as seen
  from wherever the player's eye is in the hull. Each is lit from its own
  star, in the correct occlusion order, from interplanetary distance down to
  high orbit, with no pop and no hand-off.
- **An approach you fly.** Point at a planet, open the drive and watch it grow.
  No console command involved.
- Fixed exposure, so that brightness and darkness mean something, and a veil
  on the glass, so that a lit ship hides the faint stars and a dark one shows
  them.
- All of it pure C++ arithmetic under one thin actor, headlessly tested, built
  from the engine sphere and Python-authored materials. The names the C++
  drives in those materials are checked, not trusted.

## Non-goals

- **Landing, low orbit and planetary surfaces.** The proxy trick below works
  down to about 10 km altitude. When surfaces arrive, with the reference-frame
  handoff ADR 0005 already names, it is replaced rather than extended.
- **Deciding what exists.** Star systems, planet counts, orbits and which
  neighbouring systems are real belong to procgen (ADR 0006: there is exactly
  one generator). The sky ships a placeholder system only so that it can be
  seen before procgen lands; see decision 7.
- **Destination markers, reticles, or labels painted on the sky.** Navigation
  may want to show a selected system. That is its call and its UI. The sky
  draws only what is there.
- **Orbital motion.** Bodies are fixed in the POC. Moving planets along their
  orbits is a line of Kepler in procgen's system description. The sky already
  re-projects every frame, so it costs nothing here when it arrives.
- **Atmospheres, clouds, surface texture and rings.** A planet is a lit ball
  with a noise mottle and an optional rim. Deliberately.
- **A proper in-system drive.** Decision 8's drive is the crudest thing that
  makes the approach playable. It has no spool-up, no fiction screen and no
  power draw of its own. Navigation or a flight spec owns the real one.

## Decisions

### 1. Every body is a homothety about the ship, pushed far enough out that the eye's offset stops mattering

A *homothety* is a uniform scaling centred on a point. Scale a sphere by *k*
about the camera and every ray from the camera hits the scaled sphere exactly
where it hit the original: direction, angular radius, silhouette and phase are
all identical. Only the distance changes, and distance is the one thing the
eye cannot see.

So each body is drawn as the engine sphere at

```
ProxyLocation = k * (Body - Ship)        // through FUniversePosition::operator-
ProxyRadius   = k * Radius
```

with *k* chosen per body, per frame, so that the proxy lands in a band the
renderer is comfortable with.

**The homothety is centred on the ship's origin, and the eye is not there.**
The hull runs from x = −810 (cargo bay) to x = 1760 (the cockpit glass), so
the eye can be 17.6 m forward of the centre, or anywhere else within 26 m.
Scaling about a point other than the eye keeps direction and angular size only
approximately. The error is the eye's offset divided by the proxy's distance.
The first draft put the nearest proxy at 2 km. At that distance, walking from
the galley to the cockpit slid the planet about 0.7° against the stars, where
a real world 40,000 km away moves four ten-thousandths of a degree. That is a
model hanging 2 km outside the window, which is exactly the "planet-shaped
skybox" the vision rules out.

The band is therefore set by the eye's parallax, not by the hull:

- **`NearProxy` = 50 km.** The largest offset in the ship is 17.6 m, and
  17.6 m / 50 km is 0.35 mrad, **0.68 px** at 4K. Walking the full length of
  the hull moves the nearest body by under one pixel against the star dome.
  The angular-size error is the same ratio, 0.035%.
- **`FarProxy` = 125,000 km**, keeping the first draft's 2,500× ratio, so
  none of the depth budget below changes.
- **The star dome at 250,000 km** (2.5 × 10¹⁰ cm), behind every proxy. At
  that distance a float resolves a vertex to about 20 m relative to the
  camera, which is 8 × 10⁻⁸ rad. For a star that is nothing. (The review
  estimated 1.5 m. The true spacing is coarser, and it still does not matter.)

Everything is well inside float precision relative to the camera, because
Unreal renders relative to the view origin. Proxies set `bNeverDistanceCull`,
the default far plane is infinite (reverse-Z), and the sky actor itself sits
at the origin, so world-bounds checks never see the distance.

**Below 50 km altitude the parallax is under-drawn.** There the true near
surface is closer than `NearProxy`, so *k* > 1 and the proxy is magnified.
Scaling about the ship centre then *shrinks* the eye's parallax: at the 10 km
floor the ground moves about a fifth as far as it should as you cross the
cockpit, an error of about 3 px. That is accepted. It sits in the regime
landing will replace, and it errs towards the look of a distant world rather
than a nearby model.

**Occlusion.** Two bodies with independently chosen *k* can swap depth: a moon
drawn in front of the planet it is behind, or a planet z-fighting with the
star it transits. So *k* is not chosen per body in isolation. **Bodies are
sorted by the power of the ship with respect to each sphere, `d² − R²`, the
squared length of the tangent from the ship to that sphere, and stacked in
that order.** The first body's proxy near surface sits at `NearProxy`, and
each later body's near surface sits 2% beyond the previous body's far surface.

The first draft sorted by near-surface distance, `d − R`. That is wrong for
spheres. Take a planet at d = 100 with R = 50, and a small moon at d = 60,
28° off the planet's centre: inside the disc, outside the planet, and truly in
front of it. `d − R` puts the moon behind the planet (60 > 50), so it vanishes
behind a planet it is in front of. In low orbit this bites. There a planet's
near surface is 150 km away but its limb is about 1,380 km away, so anything
seen against the disc inside that range is misordered. That includes a close
moon, or a station procgen adds later.

The power order is correct for disjoint spheres, and the proof is one line.
Along any ray from the ship, the power of the ship with respect to a sphere
equals `t₁·t₂`, the product of the distances to the two hits. If the ray hits
A before B, then `t₂(A) ≤ t₁(B)`, so `pow(A) = t₁(A)t₂(A) < t₂(A)² ≤ t₁(B)² <
t₁(B)t₂(B) = pow(B)`. Stacking in that order gives disjoint depth intervals in
a valid visibility order. Because *k* is invisible, it can be reassigned
freely every frame, even when two bodies swap order during flight, and nothing
on screen moves.

The budget is comfortable. A body's proxy occupies `(d + R)/(d − R)` of depth
ratio. That is 1.00-something for anything far away, 86× for a planet at
150 km altitude, and 1,275× at 10 km. A system of a star and eight planets,
at 150 km altitude over one of them, uses about 150× of the 2,500× available.
**The sky clamps the rendered altitude at 10 km**
(`FSkyViewParams::MinRenderedAltitude`). That is also about where the engine
sphere's facets would become the problem. Below it, landing work replaces
this.

**Rejected: the band at 2 km–5,000 km (the first draft).** It is clear of the
hull and the 400 m mote field, and that was the only criterion applied. It
missed that the criterion that matters is the eye's offset. See above.

**Rejected: centring the homothety on the camera every frame.** Each frame,
the sky would take the camera's position in the counter-frame's local space
and scale about that point. That is exact from any eye position at any
altitude, including below 50 km. It costs a dependency on the player camera
inside `Project`, and a one-frame lag, because the camera manager updates
after `TG_PostUpdateWork`. It also ends the property that none of this depends
on tick order. At 5 m/s walking pace the lag is invisible, so this is not
rejected on accuracy. It is rejected because pushing the band out gets under a
pixel with three constants, while this is a coupling. **It is the upgrade
path**, and it becomes necessary the day a ship is long enough that its
length over 50 km passes a pixel, or about 26 m. The vision says ships scale,
so that day will come. See *Fakes*.

**Rejected: a two-tier sky.** Distant bodies go on a skybox, and the near one
becomes a real-scale or scaled proxy, with a hand-off at some threshold
distance. This is what the task framing suggested, and it is the common
answer. Every game that does it shows a pop, a lighting mismatch or a change
of resolution at the hand-off. The vision puts the approach at the centre of
the game, the one moment where a visible seam is least affordable. The
homothety makes the two tiers the same thing: a skybox is just a homothety
with *k* fixed at the dome radius. With one representation from 1 AU down to
10 km altitude, there is nothing to hand off.

**Rejected: real positions under Large World Coordinates.** They run out at
0.6 AU and degrade Lumen, VSM and TSR well before that. They would also need
the counter-frame to translate, which flight spec decision 5 forbids for
exactly this reason.

**Cost to change:** low. The projection is one pure function, and replacing
it touches nothing downstream.

### 2. Geometry is honest; surface brightness is honest; only irradiance is compressed, and only points are boosted

The first draft passed every brightness through one exponent. The review
showed what that does to a resolved disc. Its true flux is surface brightness
times solid angle, and the solid angle goes as 1/d². Compress the total flux
to the half power and it goes as 1/d, so the brightness *per pixel* goes as
*d*. From the resolve at 12 million km to arrival at 40,000 km, the disc dims
300×, about eight stops, under the fixed exposure decision 6 requires. The
approach dimmed as it grew. The draft's own `M_SkyStar` kept an honest
constant surface brightness, which contradicted the one-exponent rule.

The rule now has three parts. Each is stated once and holds for planets and
stars alike.

**Angular size is exact, always.** A planet 1 AU away is sub-pixel, and it is
drawn sub-pixel. Angular size, and the *rate* at which it grows, tell the
player how far away something is and how fast they are closing. Inflate it and
the approach loses its meaning. A planet big enough to see from anywhere in
the system is the "planet-shaped skybox" `docs/vision.md` rules out by name.

**Surface brightness is honest.** A resolved disc looks exactly as bright per
pixel at 12 million km as at 40,000 km, as real surfaces do. For a planet:

```
SurfaceBrightness = Albedo * Compress(IrradianceAtBody / ReferenceIrradiance)
```

It is fixed per body, because it depends only on the body's distance from its
star and bodies do not orbit yet. For a resolved star it is
`StarSurface * Compress((T / 5800 K)^4)`, and again the ship's distance plays
no part.

**Irradiance, the star-to-receiver term, is compressed.** Real irradiance
spans absurd ranges: a planet at 30 AU receives 1/900 of the light it would at
1 AU. So that term, and only that term, passes through

```
Compress(x) = x ^ FluxGamma        // FluxGamma = 0.5; x relative to a Sun-like star at 1 AU
```

At γ = 0.5 a factor of 900 becomes a factor of 30: dim, clearly dimmer, and
still there. The same term sets the sunlight on the deck (decision 5): how
bright your own star is at your distance from it.

**Points are boosted, only while they are inflated.** Below two pixels the
body is drawn at two pixels (decision 3). There its displayed flux is

```
DisplayedFlux = SurfaceBrightness * TrueSolidAngle * Phase(alpha) * PointBoost
PointBoost    = (DrawnSolidAngle / TrueSolidAngle) ^ (1 - FluxGamma)    // 1 once the true size reaches 2 px
```

and the uniform emission is `DisplayedFlux / DrawnSolidAngle`. `Phase` is the
disc-averaged brightness of a Lambert sphere at phase angle α, the same
shading `M_SkyBody` does per pixel. So at two pixels, point and disc agree in
total flux, and the boost is exactly 1 at the moment the blend starts. Below
two pixels, the point's flux falls as 1/d instead of 1/d², at γ = 0.5. An
Earth at 1 AU, at 0.16 px, is boosted 12.5×. That is what makes planets
findable by eye before navigation exists. At a few tenths of an AU a planet
is a steady point of light among the stars, distinguishable by colour and by
sitting in the ecliptic, and brighter than it has any right to be. That is the
one lie. It is told only where the drawn size is already a lie, and it
vanishes before anyone can see a surface.

**Things that are only ever points** are the 3,000 background stars and the
neighbouring systems. They have no surface to keep honest, and their
brightness is their compressed flux, placed in the starfield's range.

**Rejected: compressing total flux through one exponent (the first draft).**
The disc dims eight stops across the approach. See above.

**Rejected: inflating small bodies to a minimum angular size so they are always
visible as discs.** It is the natural reach, and it destroys the whole point
of decision 1. This includes the local star. The navigation spec's `Primary`
draws the sun at 1.5–8°, where the truth at 2.4 AU is about 0.2° (7 px at 4K).
A seven-pixel disc under fixed exposure and bloom is still unmistakably the
sun. It is also the one object whose size tells you how far from home you are.
This spec supersedes that fake; see *Seams*.

**Rejected: honest photometry.** The universe would be black except for one
star, and the player would never see anything they were not already on top
of.

**Cost to change:** one number (`FluxGamma`, where 1.0 is fully honest). The
principle is the part to argue about: geometry and surfaces are honest, and
irradiance and points are compressed.

### 3. A body is a point until it resolves, and the resolve is a material blend, not a swap

Below about two pixels of diameter a shaded sphere is noise. Its terminator
falls between pixels, and TSR smears it into flicker. So each body has a
**point** appearance and a **disc** appearance, drawn with the same mesh and
the same material.

The drawn angular diameter is `max(true, MinPointPixels)`, which is two
pixels. A scalar material parameter, `PointBlend`, runs from 1 to 0 as the
true diameter passes from 2 to 4 pixels. At 1 the body is a point: uniform
emission at `SurfaceBrightness × Phase`, times `PointBoost` below two pixels.
At 0 it is a disc, lit per pixel from the star's true direction. Across the
whole band the total flux is continuous, because the point term is the disc's
own average. The drawn size is inflated only while the body is a point, the
only place where inflating is not a lie: a sub-pixel object has *no* visible
size, and two pixels is the smallest honest one.

Pixel angle comes from the player camera's live field of view and the
viewport width each frame. So the laptop's framed view, a later zoom and a
smaller window all resolve planets at the right moment. Under `-nullrhi` it
falls back to 90° and 1920 px.

**Rejected: two components per body, cross-faded.** Twice the draw calls and a
translucency sort problem, for no visible difference. **Rejected: a billboard
sprite for the point.** It needs a texture, which is art.

**Cost to change:** low.

### 4. The background starfield is a galaxy, not a spiral, and it moves out to 250,000 km

The existing golden-angle spiral was chosen for "even coverage where uniform
random clumps". The real sky clumps, and the clumping is the galaxy: the one
thing in the view that says there is a structure far larger than anywhere you
will ever go.

**The sky's random numbers are procgen's, and the sky waits for them.**
`SkyStarfield::Generate(Seed, Count)` draws from procgen's `FGenStream`, and
the placeholder system (decision 7) does the same. That makes **procgen's step
1 (`GenSeed`, `FGenStream`, and their tests) a hard prerequisite of this
spec's step 1.** It is small and pure: two headers and a known-value test.
Nothing else of procgen is needed. The alternative is to write the sky against
`FRandomStream` and swap it later. That would be a second RNG and a second
seeding scheme, which is exactly what ADR 0006 forbids, on top of the
placeholder system this spec already admits is a second generator. One
tolerated duplicate is enough. The dependency sits in the implementation
outline, and if procgen's step 1 has not landed, the sky's step 1 is the thing
that waits.

**The seed.** The seed is `GenSeed::Derive(Root, GenSeed::Label("sky.starfield"))`.
Until procgen's `UUniverseSubsystem` exists (its step 4), `Root` is the
counter-frame's existing constant `StarSeed`. After that it is
`UUniverseSubsystem::GetRootSeed()`, which makes the sky world-level and
shareable like everything else. The swap is one line in `LocalSystem`
(decision 7).

Samplers, per the CLAUDE.md randomness rule and ADR 0008:

- **Galactic latitude** comes from a mixture. 65% of stars follow a Laplace
  distribution around the plane (scale 0.18 rad), a thin, sharply peaked disc.
  The other 35% are isotropic: uniform in *sin* latitude, which is what "no
  preferred direction" means on a sphere. The disc component's longitude
  follows a von Mises distribution (κ = 1) toward the galactic centre, so one
  side of the band is richer. The halo component is uniform in longitude. The
  Laplace draw is `Exponential` with a random sign, and the von Mises is
  approximated by a wrapped `Normal`, which at κ = 1 is indistinguishable by
  eye.
- **Flux** comes from a Pareto with α = 1.5 (inversion on `UnitOpen`),
  truncated at 400× the faintest. α = 1.5 is not a taste. It is the Euclidean
  star-count law, N(>F) ∝ F^−3/2, for stars scattered through space. The
  truncation is the naked-eye range, about 6.5 magnitudes.
- **Temperature** comes from `LogNormalBounded(5000, 0.3, 2800, 15000)`.

**One blackbody in the project, and it lives here.** `SkyColour::Blackbody(T)`
is a pure function in the sky. Procgen's spec puts a `StarColour` static on
`UUniverseSubsystem` and says colour is "a presentation concern and belongs on
this side of the seam". The sky is further along that side than the
subsystem, and it needs the function before procgen's step 4 exists.
**Request to procgen:** drop `UUniverseSubsystem::StarColour`, or make it
forward to `SkyColour::Blackbody`. Either way there is one blackbody, and the
sky does not wait on procgen's subsystem to colour a star.

The galactic plane is the universe XY plane. That is arbitrary, and fine until
procgen has a galaxy with an orientation. When it does, the plane is its
plane.

There are 3,000 stars by default, still in one `UInstancedStaticMeshComponent`:
the counter-frame's existing `DistantStars`, which keeps its job and gets a
new generator. Colour and brightness go in **per-instance custom data**, four
floats each: R, G, B and brightness. Per-instance custom data is a feature of
instanced meshes that lets one material read a different value for each
instance without a draw call for each.

**The dome moves from 120 m to 250,000 km (2.5 × 10¹⁰ cm).** It must sit
behind every body proxy or stars draw across planets, and decision 1's band
ends at 125,000 km. Instance scale grows to keep each star at two pixels,
which is about 260 km across at the dome.

**Neighbouring systems** are the real stars procgen says are near, and the
ones navigation will send you to. They live on a second, small instanced
component on `AShipSky`, `NeighbourStars`, re-placed whenever the local system
changes. They are drawn exactly like background stars, at their true direction
and at a compressed brightness from their true distance. After a jump they
have moved and the background has not. That is the whole of galactic
parallax, and it costs nothing.

**Rejected: a sky-sphere material or cubemap.** The reasons are the same as
flight spec decision 6, and a painted sky cannot hold neighbours that move
after a jump.

**Rejected: `FRandomStream` behind an adapter marked DELETE WITH PROCGEN.**
This was the review's other option. It unblocks the sky from procgen, at the
price of a second RNG in the tree for however long the adapter survives.
Procgen's step 1 is a couple of hours of pure code, so the wait is shorter
than the debt.

**Cost to change:** low. The stars carry no gameplay.

### 5. The local star lights the deck, through the glass, and nothing else in the sky does

`AShipSky` owns one Movable `UDirectionalLightComponent`, Unreal's light for a
source at infinity, whose rays are all parallel. It points from the local star
toward the ship and is parented under the counter-frame, so it swings as the
ship turns without any code. It casts shadows (virtual shadow maps, already on
in `DefaultEngine.ini`). So the hull is dark, and **the only sunlight inside
the ship is the shape of the windows, lying on the deck and walls**.

This is the contrast the vision asks for, made literal. The ship is the one
thing that is entirely yours, and in the middle of it lies a hard-edged patch
of light from something 150 million kilometres away that does not care about
you. Set a cruise with a slow roll and walk to the galley, and the patch
crawls across the table. It is the one place where the outside physically
touches the inside. The placeholder puts the sun to starboard (decision 7), so
on day one it falls through the galley's starboard window.

Its intensity is `SunLuxAtReference × Compress(irradiance at the ship) ×
SunVisibleFraction`. The first factor is compressed irradiance, the one term
decision 2 allows to be compressed. The last factor is an **eclipse**: the
fraction of the star's disc not covered by any nearer body's disc, computed
analytically in `SkyProjection::DiscOverlapFraction`. Park behind a planet
and the ship goes dark, apart from its own lights. That is an event, chosen
by where you put the ship. Nothing about it is on a timer.

**Planets are not lit by this light.** They use unlit materials that do their
own Lambert shading from a per-body `LightDirection` parameter, the direction
from *that planet* to *its* star. A directional light has only one direction,
and a planet 0.3 AU off the sun line would show the wrong phase. Unlit also
means proxies never cast shadows, never enter Lumen and never appear in ray
tracing. That removes every way a 125,000 km sphere could leak into interior
lighting.

`build_hauler.py` deletes the level's template `DirectionalLight`. There is
exactly one sun, and the sky owns it. Glass boxes are set to cast no shadow,
so the sunlight actually comes through.

**Rejected: the sky as purely visual, with the interior lit only by ship
lamps.** It is cheaper, since one shadowed directional light is the most
expensive thing added here. It also throws away the strongest contrast
available.

**Cost to change:** low in code. If the shadow cost is a problem, it shows in
the frame time straight away.

### 6. Exposure is fixed, the sky owns it, and the glass carries the room's reflection

Auto exposure is Unreal's simulated eye adaptation, which rescales the image
toward a target brightness. It appears to be on at its defaults: nothing in
`Config/` or the level turns it off. Under it, the inverse-square law is
invisible, because the camera re-brightens a far sun. So is the dimming of
the ship's lights from the power split, because the camera re-brightens a dark
galley. It cancels both of the contrasts this game uses brightness for.

**`AShipSky` owns an unbound `UPostProcessComponent`.** With
`bUnbound = true` a post-process component applies everywhere, with no volume
to place. It sets:

- `AutoExposureMethod = AEM_Manual`, with
  `AutoExposureApplyPhysicalCameraExposure` off, so exposure is exactly
  `ds.Sky.Exposure` EV and nothing else.
- Bloom on, lens flares off. Bloom is what makes a two-pixel star read as
  bright rather than merely white. It scales with total energy, so a near star
  glares and a far one glints without any code.
- Local exposure contrast back to 1.0. The project's 0.8 compresses exactly
  the contrast this spec is trying to show.

**Which contrasts the game is built on.** With exposure fixed, a pixel's value
is its emission times one constant. That gives exactly two things:

1. **The ship's own light level is honest against an unchanging outside.**
   Divert power from the lights and the galley is darker, and it stays darker.
   The stars through the window do not change, and neither does the patch of
   sunlight on the table.
2. **Your distance from your star is visible.** A sun at 30 AU is a glint and
   its patch on the deck is a thirtieth as bright. Nothing re-brightens it.

**The first draft also claimed "turn the lights off and the stars come out",
and the mechanism could not produce it.** Under fixed exposure a star's pixel
depends only on its own emission. `M_Glass` is `MSM_Unlit` and translucent
(its `.uasset` says so), so the room's lamps add nothing to the pane, and no
light reaches an unlit star. The faint half of the starfield would be either
always visible or never visible at the galley's EV. What the draft described is
what auto exposure does, and this decision removes auto exposure.

**So it is made true on purpose, by the mechanism that makes it true in life:
reflection off the glass.** From a lit room at night you cannot see faint
stars, because the window reflects the room back at you. The glass boxes move
from the hand-authored `M_Glass` to `M_SkyGlass`, which `setup_sky_materials.py`
authors. It is translucent and unlit, and it adds an emissive **veil**,
`VeilColour × Veil × InteriorLight`, over whatever is behind it. The two
scalars come from a Material Parameter Collection, `MPC_Sky`: an asset holding
global shader parameters that any material can read and C++ can set once per
frame. `AShipSky::SyncToShip` writes `InteriorLight` from `UShipSubsystem`:
`AreLightsOn() ? GetConsumerSatisfaction(ShipPower::Lights) : 0`. It asks
every frame and stores nothing.

**The tuning target is stated, so it can be checked.** At the galley EV with
the lights fully fed, the veil sits at about the pixel value of a flux-4 star.
Under a Pareto with α = 1.5, that leaves 4^−1.5 ≈ 12% of the starfield visible
(about 375 stars, the band's bright spine and the bulge). With the lights off,
all 3,000 show. Stars really do come out, by a factor of eight, and a player
who wants to see them decides to sit in the dark. The veil is in no screenshot
of the interior, because it only exists on the glass.

The starting EV is **whatever auto exposure currently settles on in the
galley**, read off the HDR visualisation at the start of step 5. On day one
the interior looks the same as it does now, and only the outside changes.

**Rejected: dropping the claim.** It is honest and free, but "you see more of
the universe when you choose darkness" is the one link between the power split
and the outside, and it is worth one material. If the veil looks like a grey
smear rather than a reflection, drop it first; the rest of this decision does
not depend on it.

**Rejected: auto exposure with a narrowed range (±1–1.5 EV).** It would make
stars come out in the dark gently, and it keeps some adaptation drama when you
walk out of a sunbeam. It does so by partly re-brightening a far sun and a
dimmed galley, which re-cancels both contrasts listed above. It stays the
fallback if manual exposure feels dead in playtest, and it is one setting.

**Cost to change:** trivial for exposure, which is a CVar, and one material
and one collection for the veil. The decision is really a statement of which
brightness contrasts the game is built on.

### 7. The sky asks where the ship is; nobody tells it

The sky reads an `FSkySystem`: a flat list of bodies, each with a universe
position, a radius, a colour, and either an albedo or a luminosity, plus a
list of neighbouring systems as directions and distances. It is a *view type*,
deliberately smaller than whatever procgen produces. That way the sky can be
built, tested and looked at before procgen exists, and procgen's internals can
change without touching it.

**Where that system comes from is one seam, `LocalSystem`, and it holds no
state.** In the first draft, navigation pushed the current system and the
transit flag into the sky (`AShipSky::SetSystem`, `SetInTransit`). Navigation
had to find and hold the sky actor, and the authoritative answer to "which
system am I in" ended up as a copy inside a rendering actor. That is the
scattering CLAUDE.md's subsystem discipline exists to prevent. Now:

```cpp
// Source/DeepSpace/Sky/LocalSystem.h -- the only file that changes when procgen or navigation lands
namespace LocalSystem
{
    /** Changes whenever Current() would. Navigation's UShipSubsystem::GetJumpSerial()
     *  once it exists; the constant 0 until then. */
    DEEPSPACE_API int32 Serial(const UWorld* World);

    /** The system the ship is in. FromSystem(UUniverseSubsystem::GetCurrentSystem(), ...)
     *  once procgen exists; the placeholder, a constant, until then. */
    DEEPSPACE_API const FSkySystem& Current(const UWorld* World);

    /** UShipSubsystem::IsInTransit() once navigation exists; false until then. */
    DEEPSPACE_API bool InTransit(const UWorld* World);

    /** Pure: distance from Where to the nearest body's surface, cm. The drive's input. */
    DEEPSPACE_API double NearestSurfaceDistance(const FSkySystem& System, const FUniversePosition& Where);
}
```

The authoritative state stays where it already lives, or where the sibling
specs already put it. The current system is procgen's
`UUniverseSubsystem::GetCurrentSystem()`, and the change serial and transit
flag are navigation's `UShipSubsystem::GetJumpSerial()` and `IsInTransit()`.
Navigation already bumps the serial and calls `SetCurrentSystem` in the same
subsystem tick, before any actor ticks, so the two can never be seen out of
step. `AShipSky::SyncToShip` compares `LocalSystem::Serial` with the serial
it last built for, and rebuilds its proxies when they differ. This is the same
pattern navigation's spec uses for the counter-frame's `BuiltForSerial`: a
cache of a pure function, never the answer. **Nothing calls into the sky, and
nothing needs to find it.**

**The placeholder is a constant, not state.** `FSkySystem::Placeholder(Seed)`
is built once, from a constant seed, into a function-local `static const` in
`LocalSystem.cpp`. It contains a Sun-like star and five planets on log-spaced
orbits: the successive orbit ratio is log-normal around 1.7 (σ = 0.2,
truncated to 1.3–2.6), the Titius–Bode shape. Radii are log-normal by kind.
Orbital phase is uniform, the one quantity here that genuinely *is* uniform.
Inclinations are small and normal, and the home planet has one moon.

**The placeholder is built around the ship's default start rather than moving
the ship there.** The ship begins at the universe origin, facing +X, and the
placeholder places the home planet 40,000 km dead ahead with its star 90° to
starboard (+Y). The first thing the player sees through the cockpit glass is
an 18° world, half lit, with the sunlight coming in through the starboard
windows. (The first draft said "sunward side", which gives a full disc, not
a half-lit one, and it needed a `PlaceShip` call at startup to get there.
Neither is needed now.)

**This placeholder is a second generator, and ADR 0006 forbids having two.**
It is tolerated only because procgen does not exist yet. It is marked
`// DELETE WITH PROCGEN` at every site, and it is replaced inside
`LocalSystem::Current` by a one-function adapter, `FSkySystem FromSystem(const
FStarSystem&, TConstArrayView<FStarSystemStub> Neighbours)`, written by
whichever of the two sub-projects lands second.

**Rejected: the sky stores the system, and navigation pushes it (the first
draft).** See above. **Rejected: a new subsystem to hold the sky's system.**
It would be a third home for "where am I", after procgen's and navigation's.
**Rejected: waiting for procgen.** The whole value of the POC is looking out
of the window, and none of the sky's hard parts depend on what is being drawn.
**Rejected: consuming procgen's types directly in the sky.** It couples the
renderer to the generator's internals. `LocalSystem.cpp` is the one place that
coupling is allowed, and it is a function.

**Cost to change:** low, provided the placeholder really is deleted.

### 8. A crude in-system drive, because the approach has to be flown

Cruise tops out at 200 m/s, and 1 AU at that speed is two and a half years.
Navigation's spec jumps *to* a system and never *into* one, and it explicitly
defers planets and approach. In the first draft, the only way to see a planet
resolve was a console command, `ds.Sky.Approach`, which teleported the ship
along a rail and overwrote its orientation every frame. That was good for
judging the renderer and useless for judging the loop. The vision says the
approach *is the content*. A POC in which it cannot be played has not tested
the concept.

So this spec takes the crudest drive that makes it playable. It is a second
lever at the helm, **`IA_Drive`, a toggle** (proposed key `F`, checked against
`IMC_Default` for clashes when bound). With the drive off, the throttle is
today's cruise. With the drive on, the throttle's full travel means:

```
DriveSpeed = Throttle * max(MaxSpeed, Room / (DriveTau / BoosterThrust))
Room       = max(0, NearestSurfaceDistance - DriveFloor)
```

In words: at full throttle you close a tenth of the remaining room every
1.5 seconds, whatever the room is. **Distance falls exponentially, by
construction, with no easing curve.** Angular size goes as 1/*d*, so the disc
grows by the same *factor* every second: double, and double again, steadily.
That is what makes a two-minute approach legible as one continuous event
rather than a long nothing followed by a sudden planet.

- **`DriveTau` = 15 s.** From 1 AU to a 40,000 km orbit is `ln(3,740) × 15 s`,
  about two minutes. The planet is a point for the first 37 seconds and a
  world for the remaining 85, which is the vision's "a minute or two to close
  with a world".
- **`DriveFloor` = 100 km.** The drive's room runs out at 100 km altitude,
  where it hands back to cruise speed. You cannot drive into a planet, and
  below 100 km you are at 200 m/s, which is the edge of landing's regime.
- **Throttle below full stretches the time constant**, so the lever sets how
  long the approach takes. Half throttle takes four minutes. Nothing tells you
  either one is right.
- **The drive follows the nose and never touches attitude.** The pilot steers
  with W/A/S/D/Q/Z throughout, and the mouse still looks around. The first
  draft's `Approach` overwrote the orientation every frame, which is exactly
  the thing this must not do.
- **It persists when the pilot stands up**, as the throttle already does. Set
  the approach, walk to the galley and watch the world arrive through its
  window. That is the vision's "the cruise is when you live in the ship",
  applied to the most striking two minutes the ship has.
- **Near things slow you.** The drive's room is the distance to the *nearest*
  surface. Leaving a planet, you speed up as you leave. Passing close to a
  moon on the way, you slow as you pass it. Closing with the destination, you
  slow into it. This is the shape of Elite's supercruise, and it means the
  drive cannot be flown into anything.
- **Boosters on a thin allocation stretch `DriveTau`.** The effective τ is
  divided by the boosters' thrust fraction, which is already 0.25–1. A starved
  ship approaches in eight minutes, not two. It degrades and never fails
  (CLAUDE.md, *Screens, the pointer, and power*). There is no fuel, no heat,
  no timer and no cutoff.

The fiction is thin, and that is recorded: at 1 AU full throttle is about
34 c. The drive has no inertia. With the drive engaged, velocity is set along
the nose each substep rather than chased under `LinearAcceleration`, and on
disengage the speed is clamped to cruise `MaxSpeed`. Nothing reads the ship's
linear acceleration for the body yet, so nothing is thrown about. The day
something is, the drive must report zero acceleration or be given a spool.

**Where it lives.** It lives in the flight model, not the sky.
`FShipFlightState` gains `SetDriveRoom(double)`, an input like the command,
which `UShipSubsystem::Tick` sets before `Step` from
`LocalSystem::NearestSurfaceDistance`. `FShipFlightCommand` gains `bDrive`, and
`FShipFlightLimits` gains `DriveTau` and `DriveFloor`.
`UShipSubsystem::SetDriveEngaged(APawn* Commander, bool)` is gated on the
pilot exactly as `SetFlightCommand` is. It is still pure arithmetic in a
struct, tested with no world. Room is read once a frame, while the flight
state substeps at 120 Hz. At full throttle a frame closes 0.1% of the room,
and even the two-second catch-up cap closes only 13%, so the drive cannot
overshoot.

**The one remaining debug command is `ds.Sky.Goto <body> <distance_km>`.** It
is a one-shot `PlaceShip` onto the body's day side, facing it, for jumping
straight to a 30 AU sun or a 150 km orbit while tuning. It sets the
orientation once and never again. `PlaceShip` is documented as "level setup
and tests", and a tuning command is both.

**Rejected: `ds.Sky.Approach` (the first draft).** It is a teleport on rails
that you watch rather than fly, and it held the pilot's attitude for its
duration. **Rejected: leaving in-system travel unowned until a sibling spec
takes it.** The POC's centrepiece would then exist only in a console. This
drive is roughly 30 lines in `ShipFlightState.cpp` plus one input action, and
the real drive, whoever writes it, replaces those lines and keeps
`LocalSystem::NearestSurfaceDistance`. **Rejected: making every throttle
setting a drive setting (speed = Throttle × Room / τ at all times).** It has
no new key, but it replaces the 200 m/s cruise, the motes and the flight
spec's walkable pace with something that is never slow except near things.

**Cost to change:** low in code. What is expensive is the *feel*: once the
developer has flown two-minute exponential approaches, that is what the game
is. That is the point of the POC.

## Who owns what

| Piece | Owner | Kind |
|---|---|---|
| Body projection, power-order stacking, resolve blend, photometry, eclipse | `SkyProjection` in `Source/DeepSpace/Sky/SkyProjection.h/.cpp` | pure C++ |
| The one blackbody | `SkyColour` in `Source/DeepSpace/Sky/SkyColour.h/.cpp` | pure C++ |
| Background star generation (on procgen's `FGenStream`) | `SkyStarfield` in `Source/DeepSpace/Sky/SkyStarfield.h/.cpp` | pure C++ |
| `FSkySystem`, `FSkyBody`, `FSkyNeighbour`, the placeholder | `Source/DeepSpace/Sky/SkySystem.h/.cpp` | pure C++ |
| Which system, its serial, transit, nearest surface | `LocalSystem` in `Source/DeepSpace/Sky/LocalSystem.h/.cpp` | free functions; asks subsystems, stores nothing |
| Material parameter and custom-data names | `SkyMaterialContract.h` + `Tools/sky_material_contract.json` | shared contract, checked by a test |
| The in-system drive | `FShipFlightState` / `UShipSubsystem` (existing) | pure C++ + gate |
| `IA_Drive` and its binding, `ADeepSpaceCharacter::DriveAction` | `Tools/setup_flight_input.py` (existing) + character | Python commandlet + C++ |
| Body proxies, neighbour stars, sun light, exposure, veil, CVars, `ds.Sky.Goto` | `AShipSky` in `Source/DeepSpace/Sky/ShipSky.h/.cpp` | actor, attached to the counter-frame |
| Rotation, background star dome, speed motes | `AShipCounterFrame` (existing) | actor |
| `M_SkyBody`, `M_SkyStar`, `M_SkyStarfield`, `M_SkyGlass`, `MPC_Sky` (and optional `M_SkyBand`) | `Tools/setup_sky_materials.py` | Python commandlet, authors assets |
| Spawning `hauler_sky`, assigning mesh and materials, deleting the template sun, glass shadows and material | `Tools/build_hauler.py` | Python commandlet |
| Checking the built level | `Tools/verify_level.py` | Python commandlet |

Meshes: `/Engine/BasicShapes/Sphere` for everything. No modelling.

## Seams with procgen and navigation

Both sibling specs were drafted alongside this one and touch the same things.
Where they overlap, this is how they fit:

- **Procgen's step 1 comes first** (decision 4). The sky uses `GenSeed` and
  `FGenStream` and nothing else of procgen until its step 4, when
  `LocalSystem` switches to `UUniverseSubsystem`.
- **The blackbody lives in the sky** (decision 4). Procgen's
  `UUniverseSubsystem::StarColour` should be dropped, or forward to
  `SkyColour::Blackbody`.
- **Navigation does not call the sky.** The first draft had navigation's jump
  handling call `AShipSky::SetSystem` and `SetInTransit`, and that is gone.
  Navigation keeps `GetJumpSerial()` and `IsInTransit()` on `UShipSubsystem`
  as its spec already has them. `LocalSystem` asks for them, and the sky polls
  `LocalSystem`.
- **Navigation's `Primary` and `CatalogStars` are superseded.** Its `Primary`
  (the local sun, direction only, exaggerated) becomes the star body in
  `AShipSky`, drawn at true size. Its `CatalogStars` (charted systems within
  20 ly at their true direction) *are* this spec's `NeighbourStars`, and they
  move from the counter-frame to `AShipSky`. Navigation keeps `CourseMarker`,
  the transit streaks and the counter-frame's own jump-serial check. Whichever
  lands second does the move.
- **The dome moves from 120 m to 250,000 km** (decision 4). Navigation's
  `CourseMarker` must size itself in pixels at `GetDomeRadius()`, using
  `GetPixelAngle()`, not as a multiple of `DistantStarScale`.
- **The start position.** Before navigation, the ship starts at the origin and
  the placeholder is built around it. Navigation's decision 8 starts the ship
  at its home star's arrival point, about 2.4 AU out, facing the sun. When that
  lands, `LocalSystem::Current` returns the home system from procgen, or from
  navigation's catalogue if procgen is still missing. The first view becomes
  a sun dead ahead, with the drive two or three minutes from any planet. That
  is a better opening than the placeholder's, and nothing here needs to
  change for it.
- **In-system travel is this spec's decision 8** until someone owns it
  properly. Navigation's arrival standoff of 2.4 AU is about 2.5 minutes of
  drive from a planet at 1 AU from the star. That is worth knowing when
  navigation tunes it.
- **Sunlight through the glass** is navigation's optional step 11 and this
  spec's decision 5. It is built once, here.
- **Eye adaptation** is a named risk in navigation and decision 6 here. Fixed
  exposure answers it.
- **The system description.** Procgen defines `FStarSystem`/`FStarSystemStub`,
  and navigation defines `FStarCatalog`/`FStarSystemDesc`. Those two must
  agree between themselves. The sky does not care which wins, because it reads
  only `FSkySystem` through `LocalSystem`. Procgen's frozen-at-epoch orbits
  (`PlanetPosition(i, 0.0)`) give the adapter positions directly. Procgen has
  no moons yet, so the placeholder's moon goes when the placeholder does.
- **`AShipCounterFrame`, `place_counter_frame` and `UShipSubsystem` are edited
  by both this spec and navigation.** Serialise them. The sky's edits are
  `RebuildStarfield`, two defaults and the mote fade on the counter-frame, and
  the drive on the subsystem.

## The pure layer

```cpp
// Source/DeepSpace/Sky/SkySystem.h
enum class ESkyBodyKind : uint8 { Star, Planet, Moon };

struct DEEPSPACE_API FSkyBody
{
    FName Id;
    ESkyBodyKind Kind = ESkyBodyKind::Planet;
    FUniversePosition Position;
    double Radius = 0.0;                            // cm
    FLinearColor Colour = FLinearColor::White;      // albedo colour; stars: from temperature
    double Albedo = 0.3;                            // planets and moons, 0..1
    double Luminosity = 1.0;                        // stars, solar units
    double TemperatureK = 5800.0;                   // stars
    FLinearColor Rim = FLinearColor::Black;         // atmosphere rim; black = none
};

struct DEEPSPACE_API FSkyNeighbour
{
    FName SystemId;
    FVector Direction;          // unit, universe axes, from this system
    double Distance = 0.0;      // cm
    double Luminosity = 1.0;
    double TemperatureK = 5800.0;
};

struct DEEPSPACE_API FSkySystem
{
    FName SystemId;
    TArray<FSkyBody> Bodies;            // exactly one Star in the POC
    TArray<FSkyNeighbour> Neighbours;

    // DELETE WITH PROCGEN (decision 7). Built around the universe origin:
    // home planet 40,000 km along +X, its star 90 degrees to +Y.
    static FSkySystem Placeholder(uint64 Seed);
};
```

```cpp
// Source/DeepSpace/Sky/SkyProjection.h
struct DEEPSPACE_API FSkyViewParams
{
    double PixelAngle = 2.0 * 1.0 / 1920.0;        // rad per px: 2 tan(45 deg) / 1920
    double NearProxy = 5.0e6;                      // 50 km: eye parallax < 1 px across the hull
    double FarProxy = 1.25e10;                     // 125,000 km: 2,500x the near edge
    double StackGap = 1.02;
    double MinPointPixels = 2.0;
    double ResolveBandPixels = 2.0;
    double FluxGamma = 0.5;                        // irradiance and point boost only
    double StarSurface = 1.0;                      // resolved star surface brightness at 5800 K
    double MinRenderedAltitude = 1.0e6;            // 10 km
};

struct DEEPSPACE_API FSkyBodyView
{
    FVector ProxyLocation;          // counter-frame local = universe axes, cm
    double ProxyRadius = 0.0;       // cm, the drawn sphere
    FVector Direction;              // unit, universe axes, from the ship origin
    double Distance = 0.0;          // true, centre to ship, cm
    double AngularRadius = 0.0;     // true, rad
    double PointBlend = 1.0;        // 1 point, 0 shaded disc
    double SurfaceBrightness = 0.0; // honest; independent of the ship's distance
    double PointBoost = 1.0;        // > 1 only while drawn larger than true
    double Brightness = 0.0;        // what the material gets: per-pixel emission scale
    FVector LightDirection;         // unit, universe axes, body toward its star
};

struct DEEPSPACE_API FSkyFrame
{
    TArray<FSkyBodyView> Bodies;    // same order as FSkySystem::Bodies
    TArray<int32> DepthOrder;       // indices into Bodies, nearest first by d^2 - R^2
    FVector SunDirection;           // unit, universe axes, ship toward the star
    double SunIrradiance = 0.0;     // compressed, 1 at the reference
    double SunVisibleFraction = 1.0;
};

namespace SkyProjection
{
    DEEPSPACE_API FSkyFrame Project(const FSkySystem& System,
                                    const FUniversePosition& Ship,
                                    const FSkyViewParams& Params);

    /** Fraction of disc A covered by disc B, angular radii and separation in rad. */
    DEEPSPACE_API double DiscOverlapFraction(double Separation, double RadiusA, double RadiusB);

    /** Disc-averaged Lambert brightness at phase angle Alpha, 1 at full phase.
     *  Must match M_SkyBody's shading term. */
    DEEPSPACE_API double LambertPhase(double Alpha);

    DEEPSPACE_API double Compress(double Ratio, double Gamma);
}
```

`Project` works entirely in universe axes. `AShipSky` places everything in its
own local space, which is the counter-frame's, whose rotation is the whole of
the ship's attitude. So none of this code knows which way the ship points, and
none of it depends on tick order. Decision 1's band is what makes that
survivable from an eye 17 m off-centre.

```cpp
// Source/DeepSpace/Sky/SkyStarfield.h
struct DEEPSPACE_API FSkyStar
{
    FVector Direction;          // unit, universe axes
    double Flux = 1.0;          // 1 = faintest drawn
    double TemperatureK = 5000.0;
};

namespace SkyStarfield
{
    DEEPSPACE_API TArray<FSkyStar> Generate(uint64 Seed, int32 Count);     // FGenStream inside
}

// Source/DeepSpace/Sky/SkyColour.h
namespace SkyColour { DEEPSPACE_API FLinearColor Blackbody(double TemperatureK); }
```

```cpp
// Source/DeepSpace/Sky/SkyMaterialContract.h -- mirrored by Tools/sky_material_contract.json
namespace SkyMaterial
{
    // M_SkyBody
    inline const FName Colour = TEXT("Colour");                 // vector
    inline const FName LightDirection = TEXT("LightDirection"); // vector
    inline const FName Rim = TEXT("Rim");                       // vector
    inline const FName Brightness = TEXT("Brightness");         // scalar (also M_SkyStar, motes)
    inline const FName PointBlend = TEXT("PointBlend");         // scalar
    inline const FName Mottle = TEXT("Mottle");                 // scalar
    // MPC_Sky, read by M_SkyGlass
    inline const FName InteriorLight = TEXT("InteriorLight");   // scalar
    inline const FName Veil = TEXT("Veil");                     // scalar
    // M_SkyStarfield: PerInstanceCustomData 0..3 = R, G, B, brightness
    constexpr int32 StarfieldCustomData = 4;
}
```

```cpp
// Source/DeepSpace/Ship/ShipFlightState.h -- additions (decision 8)
struct FShipFlightLimits  { /* ... */ double DriveTau = 15.0; double DriveFloor = 1.0e7; };
struct FShipFlightCommand { /* ... */ bool bDrive = false; };
// FShipFlightState:
void SetDriveRoom(double NearestSurfaceDistanceCm);    // input, set by the subsystem each tick
```

## The actor

`AShipSky : AActor`, one per level, is spawned by `build_hauler.py` as
`hauler_sky`. At `BeginPlay` it finds the one `AShipCounterFrame` with
`TActorIterator` and attaches itself with `AttachToActor(Frame,
FAttachmentTransformRules::KeepRelativeTransform)`. Everything outside the
hull still hangs off the counter-frame, as flight spec decision 5 requires,
and exactly one thing still applies the inverse rotation.

Constructor components (fixed; either asset-free or given assets by the build
script):

- `USceneComponent* Root`
- `UDirectionalLightComponent* Sun`: Movable, casts shadows,
  `bAtmosphereSunLight = false`.
- `UPostProcessComponent* Exposure`: unbound, settings per decision 6.
- `UInstancedStaticMeshComponent* NeighbourStars`: `NumCustomDataFloats = 4`.

Asset slots are `UPROPERTY(EditAnywhere)` and set only by `build_hauler.py`
(ADR 0002): `BodyMesh`, `BodyMaterial` (`M_SkyBody`), `StarMaterial`
(`M_SkyStar`), `PointStarMaterial` (`M_SkyStarfield`) and `SkyParameters`
(`MPC_Sky`).

**Body proxies are created at runtime, whenever `LocalSystem::Serial` differs
from `BuiltForSerial`.** They are plain `UStaticMeshComponent`s made with
`NewObject`, attached to `Root` and registered, each with a
`UMaterialInstanceDynamic`, a runtime copy of a material whose parameters C++
can set per frame. They are not constructor subobjects, for two reasons: their
count depends on the system, and runtime components sidestep both CLAUDE.md
traps about placed actors keeping stale component hierarchies and Blueprints
holding stale templates. Each is set to no collision, no shadow,
`bAffectDynamicIndirectLighting = false`, `bAffectDistanceFieldLighting =
false`, `bVisibleInRayTracing = false` and `bNeverDistanceCull = true`.

```cpp
void SyncToShip();                               // every frame; public for tests
void RebuildFor(const FSkySystem& System);       // tests only; SyncToShip calls it on a serial change

/** For anything else that draws on the dome -- navigation's course marker. */
double GetPixelAngle() const;
double GetDomeRadius() const;
const FSkyFrame& GetLastFrame() const;           // for tests

private:
int32 BuiltForSerial = INDEX_NONE;               // a cache key, never the answer
```

There is no `SetSystem` and no `SetInTransit`. `Tick` runs in
`TG_PostUpdateWork` beside the counter-frame and calls `SyncToShip`, which
does the following:

1. Rebuilds if `LocalSystem::Serial` changed. While `LocalSystem::InTransit`,
   it hides the bodies, the sun and the neighbours.
2. Reads the flight state from `UShipSubsystem`, and the pixel angle from the
   player camera manager and viewport.
3. Calls `SkyProjection::Project`.
4. Writes each proxy's relative location and scale. The engine sphere is
   100 cm across and centre-pivoted, but that is read from its bounds, never
   assumed.
5. Writes `PointBlend`, `Brightness` and `LightDirection` into each body's
   material instance. `LightDirection` is turned into world space through the
   counter-frame's rotation first, because the material reads world-space
   normals.
6. Points and scales the sun.
7. Writes `InteriorLight` and `Veil` into `MPC_Sky`.

Every name it writes comes from `SkyMaterialContract.h`. **It never changes
the flight state.** The only write path it has is `ds.Sky.Goto`, a one-shot
console command.

Tunables are console variables, so they can be moved in play without a
rebuild (Linux has no Live Coding): `ds.Sky.Exposure`, `ds.Sky.SunLux`,
`ds.Sky.FluxGamma`, `ds.Sky.PointPixels`, `ds.Sky.StarSurface`,
`ds.Sky.Veil`, and for the drive, `ds.Drive.Tau` and `ds.Drive.Floor`. The
`UPROPERTY` or struct defaults are the starting values, and the CVars override
them.

### Changes to `AShipCounterFrame`

- `RebuildStarfield` populates `DistantStars` from `SkyStarfield::Generate`,
  writes colour and brightness to per-instance custom data, and scales each
  instance to `MinPointPixels` at the dome.
- `DistantStarRadius` default 12,000 → 2.5 × 10¹⁰ cm, and `DistantStarCount`
  160 → 3,000.
- The near-field motes fade with speed. A material instance on `NearStars`
  multiplies brightness by `saturate(1 − Speed / MoteFadeSpeed)`, with
  `MoteFadeSpeed` = 2 km/s. Today's 200 m/s cruise never reaches it. Under the
  drive, motes that would wrap every frame and strobe step aside, and the
  planets' own parallax, which at those speeds is real, takes over. Speed is
  shown by the nearest thing that can honestly show it.

### Materials, from `Tools/setup_sky_materials.py`

These are authored as assets in `/Game/Materials/Sky/`, because materials
cannot be compiled at runtime (ADR 0006). **Every parameter name comes from
`Tools/sky_material_contract.json`**, the same list `SkyMaterialContract.h`
holds, so no name is typed twice. Each material is unlit, which in Unreal
means the material's emissive colour is the final pixel and no light affects
it.

- **`M_SkyBody`** takes `Colour`, `LightDirection`, `Brightness`,
  `PointBlend`, `Rim` and `Mottle`. Emissive = `Colour × Brightness ×
  lerp(shaded, 1, PointBlend)`. The shaded term is Lambert,
  `saturate(dot(VertexNormalWS, LightDirection))`, softened by a narrow
  `smoothstep` at the terminator, the term `LambertPhase` averages. It is
  multiplied by one `Noise` node on object-space position at `Mottle`
  amplitude, so a disc reads as a world rather than a ball, and a Fresnel rim
  tinted `Rim` is added on the lit limb. The night side is black, so an unlit
  planet is a hole in the starfield, which is itself a way of seeing it.
- **`M_SkyStar`** takes `Colour` and `Brightness`. It is very bright, with
  constant surface brightness per decision 2. Bloom does the rest. The motes
  use it too.
- **`M_SkyStarfield`** reads `PerInstanceCustomData` 0–3 for colour and
  brightness. One material, 3,000 stars, one draw call.
- **`M_SkyGlass`** replaces `M_Glass` on the glass boxes. It is translucent and
  unlit, with an emissive veil of `VeilColour × MPC_Sky.Veil ×
  MPC_Sky.InteriorLight` (decision 6).
- **`MPC_Sky`** is a Material Parameter Collection with the scalars
  `InteriorLight` and `Veil`.
- **`M_SkyBand`** *(optional, cut first)* is a faint emissive glow on an
  inside-out sphere just inside the dome: `exp(−|n·z| / width)` times a noise
  term, for the unresolved Milky Way.

## Implementation outline

Steps that need the editor (compiling, running a commandlet, rebuilding the
level, running automation tests) are marked **[editor]**. They must be
serialised with each other and with any other sub-project's editor work. One
process uses the editor at a time. Writing code and Python needs nothing.

**Prerequisite: procgen's step 1** (`GenSeed`, `FGenStream`) must be merged.
The sky's step 2 does not compile without it.

1. **A numbers probe**, `Tools/sky_probe.py`: pure Python, no editor. It
   prints the approach table under the drive: true angular size, pixel size,
   stacked proxy distances and depth budget, the eye-parallax error at 17.6 m
   for every body at every distance, `SurfaceBrightness` and `PointBoost`, and
   seconds since the drive opened. It covers the placeholder system from 1 AU
   down to 10 km altitude, and confirms decisions 1, 2 and 8 before any C++
   exists. *(0.5 h, no editor)*
2. **Pure layer and contract.** `SkySystem`, `SkyProjection`, `SkyColour`,
   `SkyStarfield`, `LocalSystem` (placeholder branch only),
   `SkyMaterialContract.h`, `Tools/sky_material_contract.json`. Tests:
   `SkyProjectionTest.cpp`, `SkyStarfieldTest.cpp`, `LocalSystemTest.cpp`.
   Written without the editor, and compiled with step 5. *(4.5 h, writing
   only)*
3. **The drive, pure.** `ShipFlightState.h/.cpp` additions; tests added to
   `ShipFlightStateTest.cpp`. Written without the editor, and compiled with
   step 5. *(1.5 h, writing only)*
4. **Materials.** `Tools/setup_sky_materials.py`, modelled on `setup_hud.py`'s
   commandlet shape and reading the contract JSON. It needs no new C++, so it
   can run before the rebuild, and it must, because step 5's material-contract
   test loads these assets. Written without the editor, **run [editor]**.
   *(3 h)*
5. **`AShipSky`, the wiring, the counter-frame. [editor]** This covers the new
   actor, CVars and `ds.Sky.Goto`; `UShipSubsystem` setting drive room,
   `SetDriveEngaged`, and `ApplyAllocation` stretching τ; and
   `ADeepSpaceCharacter::DriveAction` and its handler. Tests: `ShipSkyTest.cpp`
   and `SkyMaterialContractTest.cpp`; update `ShipCounterFrameTest.cpp` for
   the new radius and count. `./rebuild.sh --force --launch` is unavoidable,
   with new classes and header changes on the counter-frame, the flight state,
   the subsystem and the character. Steps 2 and 3 compile here too, so there
   is one rebuild, not three. Then run the headless automation filter
   `DeepSpace`. *(6 h)*
6. **Input and level. [editor]**, four commandlets in sequence. **First, in
   the current level and before anything else changes,** read the EV auto
   exposure settles on in the galley and make it `ds.Sky.Exposure`'s default.
   Then:
   - `setup_flight_input.py`: `IA_Drive` and its mapping, and assign
     `drive_action` on the character's CDO.
   - `build_hauler.py`: spawn `hauler_sky` and assign its mesh, materials and
     MPC; add `unreal.DirectionalLight` to `SPACE_STRIPS`; set
     `cast_shadow = False` and `M_SkyGlass` on every `glass` box; assign
     `M_SkyStar` to the motes.
   - `verify_level.py`: exactly one `hauler_sky`, no level `DirectionalLight`
     actors, no glass box casting a shadow, every glass box on `M_SkyGlass`.
   - `check_blueprints.py`, then the `BP_DeepSpaceCharacter` strings check,
     since a `UPROPERTY` was added.

   *(2 h)*
7. **Fly it and tune by eye. [editor]** Work through the playtest list below,
   moving CVars rather than rebuilding. At the end, write the settled numbers
   back into the defaults, which is one more rebuild. *(2.5 h)*

## Testing

**`DeepSpace.Sky.Projection`** is `SkyProjectionTest.cpp`, with no world:

- **Angular size is exact from the origin.** For bodies from 10 km altitude to
  30 AU, the proxy's angular radius, `asin(ProxyRadius / |ProxyLocation|)`,
  equals the true one to 1e-9 relative, unless the body is a point, where it
  equals `MinPointPixels` exactly. This pins the function.
- **It is right from where the player stands.** With the eye 20 m from the
  origin along each axis in turn, direction and angular radius are computed
  from the eye to the proxy and from the eye to the true body. They agree to
  within one 4K pixel (5.2e-4 rad) and 0.1% respectively, for every placeholder
  body at every sampled distance where the true near surface is beyond
  `NearProxy`, and within 4 px down to the 10 km floor. This is the test the
  first draft lacked. It fails at a 2 km `NearProxy`.
- **Direction is exact**, including with the ship and body in different
  chunks (the cross-boundary case ADR 0007 demands).
- **Occlusion survives.** Each case produces proxies whose depth intervals do
  not overlap, and for every pair whose discs overlap on screen, the body a
  ray hits first is the one drawn nearer. The cases:
  - a moon behind its planet;
  - a planet transiting the star;
  - a planet at 150 km altitude with the whole system behind it;
  - **the review's counterexample**: a planet at d = 100, R = 50 and a small
    moon at d = 60, 28° off its centre. The moon must be drawn in front.
    Sorting by `d − R` fails this case.
- **The band holds.** Every proxy lies in `[NearProxy, FarProxy]` for the
  placeholder system at every distance down to `MinRenderedAltitude`. This is
  the test that fails first if someone adds a sixteenth moon.
- **The resolve.** `PointBlend` is 1 below 2 px, 0 above 4 px, and monotone
  between. `PointBoost` is exactly 1 at and above 2 px, greater than 1 below,
  and 1 everywhere when `FluxGamma` = 1. Displayed flux is continuous at 2 px
  from both sides, to 1e-9.
- **Surfaces are honest.** For a planet, and for the star, at every sampled
  distance where it is resolved, `SurfaceBrightness` and `Brightness` are
  independent of the ship's distance, to 1e-12. The first draft's 300× dimming
  fails this test.
- **Eclipse.** `DiscOverlapFraction` is 0 when the discs are apart, 1 when
  fully covered, and 0.5 for equal discs at the known half-overlap separation.
  `SunVisibleFraction` is 0 with a planet squarely between ship and star.
- **Compression.** `Compress` is monotone, and a 900× ratio becomes 30× at
  γ = 0.5.

**`DeepSpace.Sky.Starfield`**, with no world:

- Same seed, same stars; different seed, different stars.
- More than 45% of stars lie within 15° of the plane (isotropic would give
  26%).
- Every flux lies in `[1, 400]`, and every temperature in `[2800, 15000]`.
- The median flux is below 2. A Pareto with α = 1.5 has its median at
  2^(1/1.5) ≈ 1.6, while uniform on 1–400 would put it near 200. This pins the
  steep distribution against a revert to uniform.
- About 12% of stars (within ±3 points) have flux above 4. This is the veil
  target in decision 6.

**`DeepSpace.Sky.LocalSystem`**, with no world except the null-world branch:

- The placeholder is identical across calls and runs.
- From the origin, the home planet's centre is 40,000 km along +X, and the
  star's direction is within 5° of +Y.
- `NearestSurfaceDistance` is exact for a point outside one sphere and picks
  the nearest of several.
- `Serial` is 0 and `InTransit` is false before navigation. This case is
  deleted when navigation lands.

**`DeepSpace.Ship.FlightState`**, drive additions, with no world:

- Drive on, full throttle, pointed at a body with room R: after `DriveTau`
  seconds the room is R/e, to 1%.
- The room never goes below 0. The ship stops at `DriveFloor` altitude and
  goes no nearer than cruise allows.
- Attitude commands work unchanged during the drive, and the drive never
  changes orientation.
- A booster thrust fraction of 0.25 makes the effective τ four times longer.
- Disengaging clamps speed to `MaxSpeed`.
- Drive off, behaviour is identical to today. The existing tests must pass
  untouched.

**`DeepSpace.Sky.ShipSky`**, with a world, in the shape of
`ShipCounterFrameTest`:

- Spawned with a counter-frame, it attaches to it at `BeginPlay`.
- The first `SyncToShip` creates one proxy per placeholder body. `RebuildFor`
  with a different system replaces them without leaking components.
- At the default start, the home planet's proxy subtends 18.3° **measured from
  an eye at the pilot seat** (about (1585, −70, 170) cm in ship space), to
  0.1%. The sun component's forward vector is the negated `SunDirection`
  rotated by the counter-frame.
- `SyncToShip` leaves the flight state bit-identical. It never writes it.
- The post-process component is unbound and manual.

**`DeepSpace.Sky.MaterialContract`**, with no world, run after step 4:

- The names in `SkyMaterialContract.h` equal those in
  `Tools/sky_material_contract.json`, in the shape of
  `DeepSpace.Player.MovementContract`.
- Each `M_Sky*` material, loaded, exposes exactly the contract's scalar and
  vector parameters (`GetAllScalarParameterInfo` and
  `GetAllVectorParameterInfo`, which are CPU-side and fine under `-nullrhi`).
  `MPC_Sky` holds exactly its scalars.
- `M_SkyStarfield`'s expressions (editor-only data, present under
  `UnrealEditor-Cmd`) include `PerInstanceCustomData` nodes reading indices
  0, 1, 2 and 3.

`SetScalarParameterValue` on a misspelt name fails silently. This test is what
makes that a red test rather than a planet that never resolves. It is the same
class of failure as `BP_ShipConsole` passing everything until someone pressed
Play.

**`Tools/verify_level.py`**: as in step 6.

### What needs eyes

1. **The approach, flown.** From the start, find a planet by eye: a steady
   point in the ecliptic. Turn to it, open the drive, full throttle. Does the
   point become a disc without anyone noticing the moment? Does the growth
   read as one continuous approach? Does two minutes feel like the content,
   or like a wait?
2. **Walk during it.** Start an approach, stand up, walk to the galley. Does
   the world arrive through the galley window? Walking from the galley to the
   cockpit, does the planet stay still against the stars? That is decision 1,
   seen.
3. **The sunbeam.** Sit, set a slow roll, walk to the galley. Does the patch of
   light on the table make the outside feel present, or like a lighting bug?
4. **Lights off.** Kill the lights at the engineering console and look out of
   the galley window. Do the stars come out? Does the veil with the lights on
   read as a reflection or as fog on the glass?
5. **The band.** From the cockpit, does the sky read as a galaxy or as noise?
6. **A far sun.** `ds.Sky.Goto` a body at 30 AU. Is the star still obviously
   the sun?
7. **Near things slow you.** Drive past the home planet's moon. Is being slowed
   by it interesting, or a nuisance? If it is a nuisance, the drive's room
   should be the destination's, not the nearest body's. That needs a target,
   which is navigation's.
8. **Flicker.** TSR against two-pixel bright points, with the ship turning.

## What is deliberately faked, and what each fake costs later

- **The placeholder system** is a second generator. Cost: an adapter and a
  deletion in `LocalSystem.cpp` when procgen lands. If it is not deleted, the
  project has two definitions of what is in a system.
- **The homothety is centred on the ship, not the eye.** Cost: parallax is
  under a pixel only while the ship is under about 26 m long and the eye is
  above 50 km altitude. A larger ship needs `NearProxy` pushed out
  proportionally, which is cheap while the dome can follow, or the per-frame
  camera-centred homothety, which is decision 1's rejected alternative and the
  tick-order coupling it avoids.
- **Irradiance is compressed and points are boosted.** Planets are far
  brighter than real until they resolve, and the outer system is lit far more
  brightly than real. Cost: none mechanically, but everything tuned by eye
  inherits γ, and changing it later re-tunes every exposure, veil and sun
  number.
- **The drive** is a lever with no fiction, no inertia and no spool, and it
  runs at 34 c at 1 AU. Cost: the real drive replaces about 30 lines of
  `ShipFlightState.cpp`, `IA_Drive` and the τ constants. If the ship's
  acceleration ever drives the body before then, the drive must report zero or
  be given a spool first. `LocalSystem::NearestSurfaceDistance` survives.
- **The veil** is a flat emissive on the glass, not a reflection of the room.
  Cost: nothing is wasted if a real reflection replaces it. `MPC_Sky` and
  `InteriorLight` stay.
- **Bodies do not orbit.** Cost: nothing here, since the projection is already
  per-frame. `SurfaceBrightness` becomes per-frame when they do.
- **The engine sphere** shows visible facets past about 60° of apparent size.
  Cost: a generated high-resolution sphere (a `ProceduralMeshComponent`
  geosphere in one C++ function) when that becomes the thing people notice,
  or nothing if surfaces replace proxies first.
- **Flat-coloured planets with a noise mottle.** Cost: nothing is wasted.
  `M_SkyBody` gains parameters, and the geometry and projection do not change.
- **The rendered-altitude floor at 10 km.** Fly lower and the planet stops
  growing. Cost: landing replaces the proxy with real terrain in the ship's
  frame. This spec's code stops being used below orbit, which is correct.
  -- as built in landing slice (b): below 50 km over a solid world, with the
  relief grown in between 50 km and the drive floor.
- **`FSkySystem` doubles as the drive's view of what is near**, which puts a
  `Sky/` include in `UShipSubsystem.cpp`. Cost: a rename to something like
  `FLocalBodies` the day anything other than the sky and the drive reads it.
- **The galactic plane is the universe XY plane.** Cost: one rotation when
  procgen has a galaxy.
- **One star per system.** Binaries are the same code with a second sun
  component and a sum in the eclipse term. Not built.

## Risks

- **Shadowed sunlight through the glass may be expensive**, or leak through
  thin hull walls under Lumen. It is the only non-trivial render cost here,
  and it will show immediately. The fallback is shadows on and Lumen
  contribution off for the sun (`IndirectLightingIntensity = 0`), then no sun.
- **Manual exposure may make the interior feel flat** when the sun is out of
  view. The mitigation is ready (decision 6's second rejected alternative) and
  is one setting.
- **Authoring material graphs from Python is fiddly.** Per-instance custom data,
  world-space normal nodes and Material Parameter Collections in particular
  have thin Python coverage. If a node cannot be made from Python, the fallback
  is to author that one material by hand in the editor, with the graph written
  out in the script's docstring, which keeps it reviewable if not regenerable.
  The material-contract test then guards the hand-made one just as well.
- **Tiny bright points under TSR** can shimmer. Two pixels is the guess, and
  the CVar exists so it can be three.
- **Depth stacking is proven only for small systems.** A gas giant with twenty
  moons at low orbit could exceed the band. The band test fails before the
  screen does.
- **The drive may feel like a lift rather than a flight.** An exponential
  approach with no inertia can read as being winched in. This is playtest
  item 1, and it is why the throttle stays a lever rather than an autopilot.
- **Procgen's step 1 may slip**, and the sky's C++ waits on it. Steps 1 and 4
  (probe and materials) do not, and can be done meanwhile.
- **Four headers change in one rebuild** (flight state, subsystem, character,
  counter-frame), and navigation touches two of them. If navigation's window 1
  is in flight at the same time, serialise the whole of step 5 behind it
  rather than interleaving rebuilds.

## Open questions

1. **What does procgen's system description look like?** The adapter is
   trivial once it exists. The only hard requirement from this side is that
   bodies have a universe position, a radius and either an albedo or a
   luminosity, and that neighbouring systems are available as directions and
   distances.
2. **Should the drive be limited by the nearest body or by the destination?**
   Nearest is the crudest, and it cannot be flown into anything. Destination
   needs a selected target, which is navigation's, and it would not slow you
   past a moon. Playtest item 7 decides.
3. **Should neighbour stars be brighter than their true distance deserves?**
   Destinations need to be findable, and a compressed-brightness neighbour at
   5 ly is one star among 3,000. The honest answer may be that finding them is
   the chart's job rather than the sky's, which keeps the sky free of markers.
4. **Sunlight colour.** A blackbody-coloured sun lights the interior with its
   colour, so an orange star makes a warm ship. That is probably wonderful and
   possibly jarring against the neutral-cool interior lamps.
5. **Who owns the real in-system drive?** Decision 8 has only the crude one.
   Navigation is the natural owner, since it already owns where you are going.
   A flight spec is the other candidate.

## Revision notes (review of 2026-09-25)

Every issue raised was accepted; none was rebutted. Where each is answered:

1. **The homothety is centred off the eye.** Decision 1: the band is pushed
   out to 50 km–125,000 km, with the dome at 250,000 km; the per-frame
   camera-centred option is recorded as rejected and as the upgrade path; the
   20 m eye-offset test is added. The review's float-spacing figure at the
   dome was optimistic (about 20 m, not 1.5 m), and it does not change the
   conclusion.
2. **Total-flux compression dims resolved discs.** Decision 2 is rewritten:
   surface brightness is honest, only irradiance is compressed, and the point
   boost is 1 from 2 px. There is a test that surface brightness is
   independent of distance.
3. **"Lights off, stars come out" was backwards.** Decision 6: the glass veil
   makes it true on purpose, the contrasts the game is built on are stated,
   and the veil target is checkable.
4. **`d − R` is not a visibility order.** Decision 1 sorts by `d² − R²`,
   with a proof, and the counterexample is a test.
5. **The system was stored on the sky and pushed by navigation.** Decision 7:
   `LocalSystem` asks, `AShipSky` polls a serial, and `SetSystem` and
   `SetInTransit` are gone.
6. **The dependency on procgen's RNG was unresolved.** Decision 4 and the
   outline: procgen's step 1 is a hard prerequisite, and the blackbody moves
   into the sky.
7. **The approach was only reachable by console.** Decision 8: a crude drive at
   the helm; `ds.Sky.Approach` is deleted; nothing in the sky writes attitude.
8. **Material names were unchecked.** `SkyMaterialContract.h` and its JSON,
   read by the authoring script, and `DeepSpace.Sky.MaterialContract`.
