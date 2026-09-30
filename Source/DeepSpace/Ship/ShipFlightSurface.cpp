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
    // No hold is the braking curve alone, not no cap: an unset hold must
    // never be the term that wins the max.
    const double Hold = HoldSeconds > 0.0 ? D / HoldSeconds : 0.0;

    // The braking curve, stepped: with Step seconds a substep, a ship that
    // loses Margin x BrakingAccel x Step each substep from v covers v^2 / 2b
    // + v Step / 2 before it stops, so it may have the v that makes that D.
    // On this curve the speed falls by exactly b x Step a substep all the
    // way to rest -- what the boosters can do, with a fifth to spare -- where
    // the continuous sqrt(2 b D) asks for more than that in the last few
    // substeps. At 2 km/s^2 that was the last 50 m/s shed at the hard stop
    // in one frame. Written without the cancellation for a small D. Far out
    // it is the continuous curve less b x Step / 2, 6.7 m/s at full thrust.
    const double Braking = BrakingOf(BrakingAccel);
    const double Half = 0.25 * Braking * FMath::Max(Step, 0.0);
    const double Brake = Braking > 0.0 ? Braking * D / (FMath::Sqrt(Braking * D + Half * Half) + Half) : 0.0;
    const double May = FMath::Max(Hold, Brake);
    return Step > 0.0 ? FMath::Min(May, D / Step) : May;
}

double ShipFlight::Room(TConstArrayView<FFlightSurface> Surfaces, const FUniversePosition& From,
                        double GroundClearanceCm)
{
    if (Surfaces.IsEmpty())
    {
        return 0.0;
    }
    double Least = Never;
    for (const FFlightSurface& Surface : Surfaces)
    {
        const TOptional<double> Ground = GroundAt(Surface, From);
        Least = FMath::Min(Least, Ground ? *Ground - GroundClearanceCm : FloorClearance(Surface, From));
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
    const double Binds = N > 0.0 && Speed > KneeSpeed ? Speed * N : Speed * Speed / Braking;
    if (D <= Binds)
    {
        return SecondsOnCap(D, Braking, N);
    }
    return (D - Binds) / Speed + SecondsOnCap(Binds, Braking, N);
}

TOptional<double> ShipFlight::GroundAt(const FFlightSurface& Surface, const FUniversePosition& From)
{
    if (!Surface.HasGround())
    {
        return {};
    }
    const FVector Out = From - Surface.Centre;
    const double R = Out.Size();
    if (!(R > 0.0))
    {
        return -Surface.Radius;
    }
    return R - Surface.Radius - Surface.Ground->Height(FVector3d(Out / R), 0.0);
}

TOptional<double> ShipFlight::RayToGround(const FFlightSurface& Surface, const FUniversePosition& From,
                                          const FVector& Direction, double ClearanceCm, double MaxDistanceCm,
                                          int32* OutSteps, FGroundRayProof* Proof)
{
    if (OutSteps)
    {
        *OutSteps = 0;
    }
    const FVector U = Direction.GetSafeNormal();
    if (!Surface.HasGround() || U.IsZero() || !(MaxDistanceCm > 0.0))
    {
        return {};
    }
    const IGroundField& Ground = *Surface.Ground;
    const double Clear = FMath::Max(ClearanceCm, 0.0);

    // Centre-relative, in doubles: at planetary scale this keeps well under
    // a millimetre, and nothing here is astronomical.
    const FVector Start = From - Surface.Centre;
    const double StartR = Start.Size();
    if (StartR > 0.0)
    {
        const FVector3d D(Start / StartR);
        if (StartR - Surface.Radius - Ground.Height(D, 0.0) - Clear <= 0.0)
        {
            // Under already: it may always climb, and may not descend -- nor
            // go level: a level ray (the along-ground ray is one by
            // construction) has a sign of U . D that is rounding, and read
            // as a climb it let a ship whose feet were on the ground slide.
            return FVector::DotProduct(U, FVector(D)) > UnderClimbSine ? TOptional<double>() : TOptional<double>(0.0);
        }
    }

    // Along: how far down the ray the centre's foot lies (positive heading
    // in, as in RayToFloor), so the shell's roots are Along -/+ Half. Miss:
    // how far the ray passes from the centre, from a cross product rather
    // than two planetary squares agreeing to every digit that matters.
    const double Shell = Surface.Radius + Ground.MaxHeightCm() + Clear;
    const double Along = -FVector::DotProduct(Start, U);
    const double Miss2 = FVector::CrossProduct(Start, U).SizeSquared();
    const double Half2 = Shell * Shell - Miss2;
    if (Half2 <= 0.0)
    {
        return {};
    }
    const double Half = FMath::Sqrt(Half2);
    const double Exit = Along + Half;
    if (Exit <= 0.0)
    {
        return {};
    }
    const double End = FMath::Min(Exit, MaxDistanceCm);
    const double Lipschitz = FMath::Sqrt(1.0 + FMath::Square(Ground.MaxSlope()));

    // An earlier march's balls are facts about this ground only if it is the
    // same ground, in the same place, at the same clearance.
    TArray<FVector> OldPoints;
    TArray<double> OldRadii;
    if (Proof)
    {
        if (Proof->Ground == Surface.Ground && Proof->Centre == Surface.Centre && Proof->Radius == Surface.Radius
            && Proof->ClearanceCm == Clear)
        {
            OldPoints = MoveTemp(Proof->Points);
            OldRadii = MoveTemp(Proof->Radii);
        }
        Proof->Reset();
        Proof->Ground = Surface.Ground;
        Proof->Centre = Surface.Centre;
        Proof->Radius = Surface.Radius;
        Proof->ClearanceCm = Clear;
    }
    int32 LastKept = INDEX_NONE;
    auto Keep = [&](const FVector& Point, double Radius)
    {
        if (Proof)
        {
            Proof->Points.Add(Point);
            Proof->Radii.Add(Radius);
        }
    };

    double T = FMath::Max(0.0, Along - Half);
    double Step = 0.0;
    int32 Fresh = 0;
    int32 Cursor = 0;
    while (true)
    {
        if (T >= End)
        {
            return {};
        }
        const FVector P = Start + U * T;

        // Inside an old ball the ray is clear to the ball's far side. The
        // balls lie in the order the old ray met them, so only the few about
        // where this one has reached can hold it.
        while (Cursor < OldPoints.Num() && ((OldPoints[Cursor] - Start) | U) + OldRadii[Cursor] < T)
        {
            ++Cursor;
        }
        double Reach = T;
        int32 Ball = INDEX_NONE;
        for (int32 K = Cursor; K < FMath::Min(Cursor + 4, OldPoints.Num()); ++K)
        {
            const FVector W = P - OldPoints[K];
            const double C = W.SizeSquared() - FMath::Square(OldRadii[K]);
            if (C < 0.0)
            {
                const double B = W | U;
                const double Far = T - B + FMath::Sqrt(B * B - C);
                if (Far > Reach)
                {
                    Reach = Far;
                    Ball = K;
                }
            }
        }
        if (Reach > T + 1.0)
        {
            if (Ball != LastKept)
            {
                if (Proof && Proof->Points.Num() >= FGroundRayProof::MaxBalls)
                {
                    return T;
                }
                Keep(OldPoints[Ball], OldRadii[Ball]);
                LastKept = Ball;
            }
            T = Reach;
            continue;
        }

        if (Fresh >= GroundMarchSteps || (Proof && Proof->Points.Num() >= FGroundRayProof::MaxBalls))
        {
            return T; // exhausted: a hit at the last proven-clear distance, never "no hit"
        }
        ++Fresh;
        if (OutSteps)
        {
            *OutSteps = Fresh;
        }
        const double R = P.Size();
        const FVector3d D(P / R);
        const double Footprint = 0.5 * Step;
        const double Above = R - Surface.Radius - Ground.Height(D, Footprint) - Ground.OmittedBoundCm(Footprint) - Clear;
        if (Above < 1.0)
        {
            return T;
        }
        Step = Above / Lipschitz;
        Keep(P, Step);
        LastKept = INDEX_NONE;
        T += Step;
    }
}

double ShipFlight::GroundApproachSpeed(double D, double BrakingAccel, double ApproachSeconds,
                                       double TouchdownSpeed, double Step)
{
    if (!(D > 0.0))
    {
        return 0.0;
    }
    const double N = FMath::Max(ApproachSeconds, MinApproachSeconds);
    const double B = BrakingMargin * FMath::Max(BrakingAccel, 0.0);
    const double D1 = B * N * N;
    const double Law = D <= D1 ? D / N : FMath::Sqrt(FMath::Square(D1 / N) + 2.0 * B * (D - D1));
    const double May = FMath::Max(FMath::Max(TouchdownSpeed, 0.0), Law);
    return Step > 0.0 ? FMath::Min(May, D / Step) : May;
}

double ShipFlight::SkimCap(double AglCm, double SkimSeconds, double SkimFloor)
{
    return FMath::Max(SkimFloor, FMath::Max(AglCm, 0.0) / FMath::Max(SkimSeconds, 1.0e-3));
}

double ShipFlight::SecondsToGround(double PathCm, double Speed, double PathSine, const FGroundLaw& Law)
{
    if (!(PathCm > 0.0))
    {
        return 0.0;
    }
    if (!(Speed > 0.0))
    {
        return Never;
    }
    const double S = FMath::Clamp(PathSine, 1.0e-6, 1.0);
    const double C = FMath::Sqrt(FMath::Max(0.0, 1.0 - S * S));
    const auto MayAlong = [&](double L)
    {
        const double Down = GroundApproachSpeed(L * S, Law.BrakingAccel, Law.ApproachSeconds, Law.TouchdownSpeed, 0.0) / S;
        const double Skim = C > 1.0e-9 ? SkimCap(L * S, Law.SkimSeconds, Law.SkimFloor) / C : Never;
        return FMath::Min(Speed, FMath::Min(Down, Skim));
    };
    constexpr int32 Steps = 512;
    const double Last = FMath::Min(1.0, PathCm);
    const double Ratio = FMath::Pow(Last / PathCm, 1.0 / Steps);
    double Seconds = 0.0;
    double L = PathCm;
    for (int32 I = 0; I < Steps; ++I)
    {
        const double Next = L * Ratio;
        Seconds += (L - Next) / MayAlong(FMath::Sqrt(L * Next));
        L = Next;
    }
    return Seconds + Last / MayAlong(0.5 * Last);
}
