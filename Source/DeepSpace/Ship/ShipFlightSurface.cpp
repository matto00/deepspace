#include "Ship/ShipFlightSurface.h"

#include <limits>

namespace
{
    constexpr double Never = std::numeric_limits<double>::infinity();

    /** v^2 = Braking x d on the braking curve. */
    double BrakingOf(double BrakingAccel)
    {
        return 2.0 * ShipFlight::BrakingMargin * FMath::Max(BrakingAccel, 0.0);
    }

    /** Seconds from d to the floor for a ship already on the cap: e-folds of
     *  HoldSeconds to the knee, then the braking curve's 2 sqrt(d / Braking). */
    double SecondsOnCap(double D, double Braking, double HoldSeconds)
    {
        const double Knee = Braking * HoldSeconds * HoldSeconds;
        if (HoldSeconds > 0.0 && D > Knee)
        {
            return HoldSeconds * FMath::Loge(D / Knee) + 2.0 * HoldSeconds;
        }
        return 2.0 * FMath::Sqrt(D / Braking);
    }
}

TOptional<double> ShipFlight::RayToFloor(const FFlightSurface& Surface, const FUniversePosition& From,
                                         const FVector& Direction)
{
    const FVector U = Direction.GetSafeNormal();
    if (U.IsZero())
    {
        return {};
    }

    // Through the chunk index, never the offsets (ADR 0007).
    const FVector ToCentre = Surface.Centre - From;
    const double Distance = ToCentre.Size();
    const double Floor = FMath::Max(Surface.FloorRadius(), 0.0);

    // Along: how far down the ray the centre's foot lies. Miss: how far the
    // ray passes from the centre, from a cross product rather than
    // Distance^2 - Along^2, which at a limb from far away is two huge
    // numbers agreeing to every digit that matters.
    const double Along = FVector::DotProduct(U, ToCentre);
    const double Miss = FVector::CrossProduct(ToCentre, U).Size();
    const double Chord = (Floor - Miss) * (Floor + Miss);          // half-chord^2
    const double Power = (Distance - Floor) * (Distance + Floor);  // tangent length^2

    if (!Surface.bInsideOut)
    {
        if (Distance <= Floor)
        {
            return Along > 0.0 ? TOptional<double>(0.0) : TOptional<double>();
        }
        if (Along <= 0.0 || Chord < 0.0)
        {
            return {};
        }
        // The near root, Along - sqrt(Chord), as Power / (Along + sqrt(Chord)):
        // the same number without the cancellation when the floor is close.
        return Power / (Along + FMath::Sqrt(Chord));
    }

    if (Distance >= Floor)
    {
        if (Along <= 0.0)
        {
            return 0.0;
        }
        if (Chord < 0.0)
        {
            return {};
        }
        return Along + FMath::Sqrt(Chord);
    }

    // Inside the edge: the far root. Behind the centre's foot it is
    // -Power / (sqrt(Chord) - Along), the same root without cancellation.
    const double Half = FMath::Sqrt(FMath::Max(Chord, 0.0));
    return Along >= 0.0 ? Along + Half : -Power / (Half - Along);
}

double ShipFlight::FloorClearance(const FFlightSurface& Surface, const FUniversePosition& From)
{
    const double Distance = (Surface.Centre - From).Size();
    return Surface.bInsideOut ? Surface.FloorRadius() - Distance : Distance - Surface.FloorRadius();
}

double ShipFlight::MaySpeed(double D, double BrakingAccel, double HoldSeconds, double Step)
{
    if (!(D > 0.0))
    {
        return 0.0;
    }
    const double Hold = HoldSeconds > 0.0 ? D / HoldSeconds : Never;
    const double Brake = FMath::Sqrt(BrakingOf(BrakingAccel) * D);
    const double May = FMath::Max(Hold, Brake);
    return Step > 0.0 ? FMath::Min(May, D / Step) : May;
}

double ShipFlight::Room(TConstArrayView<FFlightSurface> Surfaces, const FUniversePosition& From)
{
    if (Surfaces.IsEmpty())
    {
        return 0.0;
    }
    double Least = Never;
    for (const FFlightSurface& Surface : Surfaces)
    {
        Least = FMath::Min(Least, FloorClearance(Surface, From));
    }
    return FMath::Max(Least, 0.0);
}

double ShipFlight::SecondsToFloor(double D, double Speed, double BrakingAccel, double HoldSeconds)
{
    if (!(Speed > 0.0))
    {
        return Never;
    }
    if (!(D > 0.0))
    {
        return 0.0;
    }
    const double Braking = BrakingOf(BrakingAccel);
    if (Braking <= 0.0)
    {
        return Never;
    }

    // Where the cap binds: MaySpeed(d) = Speed, on the hold above the
    // braking knee and on the braking curve below it.
    const double N = FMath::Max(HoldSeconds, 0.0);
    const double KneeSpeed = Braking * N;
    const double Binds = Speed > KneeSpeed ? Speed * N : Speed * Speed / Braking;
    if (D <= Binds)
    {
        return SecondsOnCap(D, Braking, N);
    }
    return (D - Binds) / Speed + SecondsOnCap(Binds, Braking, N);
}
