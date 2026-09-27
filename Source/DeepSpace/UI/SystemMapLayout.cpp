#include "UI/SystemMapLayout.h"

#include "Ship/NavStart.h"
#include "Universe/SystemNames.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** A distance too small to have a direction, cm: a millimetre. */
    constexpr double NoDirectionCm = 0.1;

    /** ln 10: d(log10 x)/dx is 1 / (x ln 10). */
    constexpr double Ln10 = 2.302585092994045684;

    /** The panel direction of a universe-plane offset: +X up the glass, +Y
     *  to the right. Up with no offset at all. */
    FVector2D PanelDirection(double UniverseX, double UniverseY)
    {
        const FVector2D Dir(UniverseY, -UniverseX);
        const double Length = Dir.Size();
        return Length > UE_SMALL_NUMBER ? Dir / Length : FVector2D(0.0, -1.0);
    }

    /** A panel vector from a universe-plane one, unnormalised: the same
     *  axes as PanelDirection. */
    FVector2D PanelVector(double UniverseX, double UniverseY)
    {
        return FVector2D(UniverseY, -UniverseX);
    }

    /** The least radius the ship's glyph is drawn at, px: held clear of the
     *  star's disc (see Ship). */
    double GlyphFloorPx(const SystemMap::FMapScale& Scale)
    {
        return Scale.Pixels.StarPx + 0.5 * SystemMap::ShipRingPx;
    }
}

double SystemMap::MinRingGap(const FMapPixels& Pixels)
{
    return FMath::FloorToDouble((Pixels.RimPx - Pixels.StarPx) / (FMath::Max(Pixels.MaxPlanets, 0) + 1));
}

double SystemMap::FMapScale::RadiusPx(double DistanceAU) const
{
    if (KnotLog.Num() < 2 || DistanceAU <= InnerAU)
    {
        return Pixels.StarPx;
    }
    if (DistanceAU >= RimAU)
    {
        return Pixels.RimPx;
    }

    const double Log = FMath::LogX(10.0, DistanceAU);
    for (int32 Knot = 1; Knot < KnotLog.Num(); ++Knot)
    {
        if (Log <= KnotLog[Knot])
        {
            const double Span = KnotLog[Knot] - KnotLog[Knot - 1];
            const double T = Span > 0.0 ? (Log - KnotLog[Knot - 1]) / Span : 1.0;
            return FMath::Lerp(KnotPx[Knot - 1], KnotPx[Knot], FMath::Clamp(T, 0.0, 1.0));
        }
    }
    return Pixels.RimPx;
}

double SystemMap::FMapScale::RadiusSlopePxPerAU(double DistanceAU, bool bOutward) const
{
    // Clamped at either end: the side about to be crossed is flat.
    if (KnotLog.Num() < 2 || DistanceAU <= 0.0
        || (bOutward ? DistanceAU < InnerAU || DistanceAU >= RimAU
                     : DistanceAU <= InnerAU || DistanceAU > RimAU))
    {
        return 0.0;
    }

    // The segment the ship is about to be in: probed a hair to that side.
    // Exactly on an orbit -- where every ship sits at a world -- the two
    // sides' slopes differ, and the ship's log and the knot's, computed
    // apart, disagree in the last bit; a bare comparison picked the wrong
    // side as often as the right one.
    constexpr double ProbeDex = 1.0e-9;
    const double Log = FMath::LogX(10.0, DistanceAU) + (bOutward ? ProbeDex : -ProbeDex);
    for (int32 Knot = 1; Knot < KnotLog.Num(); ++Knot)
    {
        if (Log < KnotLog[Knot])
        {
            const double Span = KnotLog[Knot] - KnotLog[Knot - 1];
            if (Span <= 0.0)
            {
                return 0.0;
            }
            // d(px)/d(log10 AU), then d(log10 AU)/d(AU) = 1 / (AU ln 10).
            return (KnotPx[Knot] - KnotPx[Knot - 1]) / Span / (DistanceAU * Ln10);
        }
    }
    return 0.0;
}

SystemMap::FMapScale SystemMap::Fit(const FStarSystem& System, double StandoffAU, const FMapPixels& Pixels)
{
    FMapScale Scale;
    Scale.Pixels = Pixels;
    Scale.Star = System.Stub.Position;

    const int32 Worlds = System.Planets.Num();
    const double StarRadiusAU = System.Star.RadiusSolar * UniverseUnits::CmPerSolarRadius / UniverseUnits::CmPerAU;
    const double OutermostAU = Worlds > 0 ? System.Planets.Last().SemiMajorAxisAU : 0.0;

    // The rim is where the jump lets go of this system, with a margin: the
    // arrival is always on the map, and so, because the standoff is never
    // less than 1.5 times the outermost orbit, is every world. Held outside
    // the outermost orbit regardless, so a mistuned standoff cannot fold
    // the warp back on itself.
    Scale.RimAU = FMath::Max(NavStart::ArrivalStandoffAU(System, StandoffAU) * RimMargin, OutermostAU * 1.5);

    // Half the innermost orbit maps to the star's edge. With no worlds the
    // star's own surface does: there is nothing to make room for.
    Scale.InnerAU = Worlds > 0 ? 0.5 * System.Planets[0].SemiMajorAxisAU : StarRadiusAU;
    Scale.InnerAU = FMath::Clamp(Scale.InnerAU, UE_DOUBLE_SMALL_NUMBER, Scale.RimAU * 0.5);

    const double LogIn = FMath::LogX(10.0, Scale.InnerAU);
    const double LogRim = FMath::LogX(10.0, Scale.RimAU);
    const double Span = Pixels.RimPx - Pixels.StarPx;
    const double Gap = MinRingGap(Pixels);

    // 1-2. Each orbit where a pure log map puts it.
    TArray<double> Rings;
    Rings.Reserve(Worlds);
    for (const FPlanet& Planet : System.Planets)
    {
        const double Log = FMath::LogX(10.0, FMath::Max(Planet.SemiMajorAxisAU, UE_DOUBLE_SMALL_NUMBER));
        Rings.Add(Pixels.StarPx + (Log - LogIn) / (LogRim - LogIn) * Span);
    }

    // 4. The warp. Outward, each ring at least a gap past the one inside it
    // (the first, a gap past the star's edge); then inward, the outermost
    // held a gap inside the rim and each ring at least a gap inside the one
    // outside it. The rim is a neighbour like the star's edge: the segment
    // between the outermost orbit and the rim is where the arrival is drawn,
    // and a ship coming in must be seen to move across it. MinRingGap is
    // derived so that the inward pass can never push the first ring into
    // the star, which is why this is two passes and not a proportional
    // squeeze: that would break the very gap it exists to keep.
    for (int32 Ring = 0; Ring < Worlds; ++Ring)
    {
        const double Floor = Ring == 0 ? Pixels.StarPx + Gap : Rings[Ring - 1] + Gap;
        Rings[Ring] = FMath::Max(Rings[Ring], Floor);
    }
    for (int32 Ring = Worlds - 1; Ring >= 0; --Ring)
    {
        const double Ceiling = Ring == Worlds - 1 ? Pixels.RimPx - Gap : Rings[Ring + 1] - Gap;
        Rings[Ring] = FMath::Min(Rings[Ring], Ceiling);
    }

    // 5. The knots, through which everything else is placed.
    Scale.KnotLog.Add(LogIn);
    Scale.KnotPx.Add(Pixels.StarPx);
    for (int32 Ring = 0; Ring < Worlds; ++Ring)
    {
        Scale.KnotLog.Add(FMath::LogX(10.0, FMath::Max(System.Planets[Ring].SemiMajorAxisAU, UE_DOUBLE_SMALL_NUMBER)));
        Scale.KnotPx.Add(Rings[Ring]);
    }
    Scale.KnotLog.Add(LogRim);
    Scale.KnotPx.Add(Pixels.RimPx);
    Scale.RingPx = MoveTemp(Rings);
    return Scale;
}

FVector2D SystemMap::Place(const FMapScale& Scale, const FUniversePosition& Where)
{
    const FVector Offset = Where - Scale.Star;
    const double RadiusPx = Scale.RadiusPx(Offset.Size() / UniverseUnits::CmPerAU);
    return Scale.Pixels.Centre + PanelDirection(Offset.X, Offset.Y) * RadiusPx;
}

TOptional<FVector2D> SystemMap::MotionOnMap(const FMapScale& Scale, const FUniversePosition& Where,
                                           const FVector& Direction)
{
    const FVector Offset = Where - Scale.Star;
    const double Distance = Offset.Size();
    const double InPlane = FVector2D(Offset.X, Offset.Y).Size();
    const FVector Step = Direction.GetSafeNormal();
    if (InPlane <= NoDirectionCm || Step.IsNearlyZero())
    {
        return {};
    }

    // Ship draws the glyph at R(|offset|) along the offset's azimuth in the
    // plane. Its derivative along Step, per cm, has two parts:
    //  - radial: R' times the rate the true distance changes, along the
    //    azimuth's panel direction;
    //  - round: R times the rate the azimuth turns, which is Step's in-plane
    //    part square to the offset over the in-plane distance.
    // The second is R / r of the first's scale R', which for a log map is
    // several times larger: the map is stretched round each ring, and a
    // direction taken straight from the universe misreads it.
    const double DistanceAU = Distance / UniverseUnits::CmPerAU;
    const double Closing = FVector::DotProduct(Offset, Step) / Distance;
    const double RawRadiusPx = Scale.RadiusPx(DistanceAU);
    const double RadiusPx = FMath::Max(RawRadiusPx, GlyphFloorPx(Scale));
    const double SlopePxPerCm = RawRadiusPx < GlyphFloorPx(Scale)
        ? 0.0
        : Scale.RadiusSlopePxPerAU(DistanceAU, Closing > 0.0) / UniverseUnits::CmPerAU;

    const FVector2D Radial(Offset.X / InPlane, Offset.Y / InPlane);
    const FVector2D Flat(Step.X, Step.Y);
    const FVector2D Round = Flat - FVector2D::DotProduct(Flat, Radial) * Radial;

    const FVector2D Motion = PanelVector(Radial.X, Radial.Y) * (SlopePxPerCm * Closing)
                           + PanelVector(Round.X, Round.Y) * (RadiusPx / InPlane);
    const double Length = Motion.Size();
    if (Length <= UE_DOUBLE_SMALL_NUMBER * FMath::Max(SlopePxPerCm, RadiusPx / InPlane))
    {
        return {};
    }
    return Motion / Length;
}

SystemMap::FMapShip SystemMap::Ship(const FMapScale& Scale, const FUniversePosition& Where, const FQuat& Orientation)
{
    FMapShip Glyph;
    const FVector Offset = Where - Scale.Star;
    const double Distance = Offset.Size();
    const double DistanceAU = Distance / UniverseUnits::CmPerAU;

    Glyph.Pin = DistanceAU <= Scale.InnerAU ? EMapPin::Inside
              : DistanceAU >= Scale.RimAU ? EMapPin::Beyond
              : EMapPin::None;
    Glyph.ElevationDeg = Distance > NoDirectionCm
        ? FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Offset.Z / Distance, -1.0, 1.0)))
        : 0.0;

    // Held clear of the star's disc: the disc stands for the space inside
    // the innermost knot, and a glyph drawn over it would read as "in the
    // star". Every ring is at least a gap outside the disc, so this never
    // moves a ship that is on an orbit.
    const double RadiusPx = FMath::Max(Scale.RadiusPx(DistanceAU), GlyphFloorPx(Scale));
    Glyph.Centre = Scale.Pixels.Centre + PanelDirection(Offset.X, Offset.Y) * RadiusPx;

    const FVector Nose = Orientation.RotateVector(FVector::ForwardVector).GetSafeNormal();
    if (FMath::Abs(Nose.Z) < FMath::Cos(FMath::DegreesToRadians(NoseHiddenWithinDeg)))
    {
        // The way the glyph goes when the ship flies nose first, so the
        // tick and the glyph's motion are one. Where the glyph cannot move
        // (pinned, heading straight in or out; or on the star's axis) the
        // nose's own direction in the plane is all there is to show.
        const TOptional<FVector2D> Along = MotionOnMap(Scale, Where, Nose);
        Glyph.Nose = Along ? *Along : PanelDirection(Nose.X, Nose.Y);
    }
    return Glyph;
}

SystemMap::FMapLayout SystemMap::Layout(const FStarSystem& System, const FMapScale& Scale)
{
    FMapLayout Drawn;
    Drawn.Pixels = Scale.Pixels;
    Drawn.RingPx = Scale.RingPx;

    const int32 Worlds = FMath::Min(System.Planets.Num(), Scale.RingPx.Num());
    for (int32 Orbit = 0; Orbit < Worlds; ++Orbit)
    {
        const FPlanet& Planet = System.Planets[Orbit];
        FMapDot Dot;
        Dot.Orbit = Orbit;
        Dot.RingPx = Scale.RingPx[Orbit];

        // On its ring at its true phase: the ring's radius, not the warp of
        // the position's distance, so the dot is on the ring to the pixel.
        const FVector Offset = System.PlanetPosition(Orbit) - Scale.Star;
        Dot.Outward = PanelDirection(Offset.X, Offset.Y);
        Dot.Centre = Scale.Pixels.Centre + Dot.Outward * Dot.RingPx;

        // The room either side: the next ring, or the star's edge inward and
        // the rim outward. The rim is a neighbour because the arrival is
        // drawn between it and the outermost ring, and the ship's glyph
        // there must not sit on the outermost dot. The warp keeps it at
        // least a gap away, so this never shrinks a dot the ring inside it
        // would not.
        const double Inward = Dot.RingPx - (Orbit == 0 ? Scale.Pixels.StarPx : Scale.RingPx[Orbit - 1]);
        const double Outward = (Orbit + 1 < Worlds ? Scale.RingPx[Orbit + 1] : Scale.Pixels.RimPx) - Dot.RingPx;
        Dot.GapPx = FMath::Min(Inward, Outward);

        const double Base = Planet.Kind == EPlanetKind::GasGiant ? GiantDotPx : RockDotPx;
        Dot.SizePx = FMath::Min(Base, Dot.GapPx - DotClearancePx);
        Dot.TargetRingPx = FMath::Min(Dot.SizePx + TargetPadPx, 2.0 * Dot.GapPx - DotClearancePx);
        Dot.Numeral = SystemNames::RomanNumeral(Orbit + 1);
        Drawn.Dots.Add(MoveTemp(Dot));
    }
    return Drawn;
}

TOptional<int32> SystemMap::Pick(const FMapLayout& Layout, const FVector2D& Point, double MaxRadius)
{
    if (FVector2D::Distance(Point, Layout.Pixels.Centre) <= Layout.Pixels.StarPx)
    {
        return {};
    }

    // Innermost first and replaced only by a strictly nearer dot, so an
    // exact tie stays with the inner world.
    TOptional<int32> Nearest;
    double NearestDistance = MaxRadius;
    for (const FMapDot& Dot : Layout.Dots)
    {
        const double Distance = FVector2D::Distance(Point, Dot.Centre);
        if (Distance < NearestDistance || (!Nearest && Distance <= MaxRadius))
        {
            Nearest = Dot.Orbit;
            NearestDistance = Distance;
        }
    }
    return Nearest;
}

TOptional<SystemMap::FMapSelection> SystemMap::Select(const FStarSystem& Here, int32 Orbit,
                                                     const TOptional<FBodyId>& Target)
{
    if (!Here.Planets.IsValidIndex(Orbit))
    {
        return {};
    }
    FMapSelection Selection;
    Selection.Body = FBodyId{Here.Stub.Id, Orbit, -1};
    Selection.Action = (Target && *Target == Selection.Body) ? EMapSelect::Clear : EMapSelect::Target;
    return Selection;
}
