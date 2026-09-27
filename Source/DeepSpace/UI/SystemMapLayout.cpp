#include "UI/SystemMapLayout.h"

#include "Ship/NavStart.h"
#include "Universe/SystemNames.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** A distance too small to have a direction, cm: a millimetre. */
    constexpr double NoDirectionCm = 0.1;

    /** The panel direction of a universe-plane offset: +X up the glass, +Y
     *  to the right. Up with no offset at all. */
    FVector2D PanelDirection(double UniverseX, double UniverseY)
    {
        const FVector2D Dir(UniverseY, -UniverseX);
        const double Length = Dir.Size();
        return Length > UE_SMALL_NUMBER ? Dir / Length : FVector2D(0.0, -1.0);
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
    const double RadiusPx = FMath::Max(Scale.RadiusPx(DistanceAU), Scale.Pixels.StarPx + 0.5 * ShipRingPx);
    Glyph.Centre = Scale.Pixels.Centre + PanelDirection(Offset.X, Offset.Y) * RadiusPx;

    const FVector Nose = Orientation.RotateVector(FVector::ForwardVector).GetSafeNormal();
    if (FMath::Abs(Nose.Z) < FMath::Cos(FMath::DegreesToRadians(NoseHiddenWithinDeg)))
    {
        Glyph.Nose = PanelDirection(Nose.X, Nose.Y);
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
