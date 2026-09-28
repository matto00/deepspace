#include "Ship/ShipLanding.h"

TArray<FVector, TFixedAllocator<8>> ShipLanding::FootprintPoints(double GearClearanceCm)
{
    TArray<FVector, TFixedAllocator<8>> Points;
    for (const auto& Foot : GearFeetXY)
    {
        Points.Add(FVector(Foot[0], Foot[1], -GearClearanceCm));
    }
    for (const auto& Corner : BellyCorners)
    {
        Points.Add(FVector(Corner[0], Corner[1], Corner[2]));
    }
    return Points;
}

double ShipLanding::ReachCm(double GearClearanceCm)
{
    double Reach = 0.0;
    for (const FVector& Point : FootprintPoints(GearClearanceCm))
    {
        Reach = FMath::Max(Reach, Point.Size());
    }
    return Reach;
}

ShipLanding::FFootprintClearance ShipLanding::FootprintClearance(const FFlightSurface& Surface, const FUniversePosition& Origin,
                                                                 const FQuat& Orientation, double GearClearanceCm)
{
    FFootprintClearance Out;
    if (!Surface.HasGround())
    {
        return Out;
    }
    const FVector FromCentre = Origin - Surface.Centre;
    const TArray<FVector, TFixedAllocator<8>> Points = FootprintPoints(GearClearanceCm);
    FVector3d LeastD = FVector3d::UnitZ();
    for (int32 Index = 0; Index < Points.Num(); ++Index)
    {
        const FVector At = FromCentre + Orientation.RotateVector(Points[Index]);
        const double R = At.Size();
        const FVector3d D(At / R);
        const double Above = R - Surface.Radius - Surface.Ground->Height(D, 0.0);
        if (Above < Out.Least)
        {
            Out.Least = Above;
            Out.Point = Index;
            LeastD = D;
        }
    }
    Out.GroundNormal = FVector(ShipGround::NormalAt(*Surface.Ground, LeastD));
    return Out;
}
