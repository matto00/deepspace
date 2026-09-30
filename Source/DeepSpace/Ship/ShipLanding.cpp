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

TArray<ShipLanding::FFootprintHeight, TFixedAllocator<8>> ShipLanding::FootprintHeights(const FFlightSurface& Surface, const FUniversePosition& Origin,
                                                                                       const FQuat& Orientation, double GearClearanceCm)
{
    TArray<FFootprintHeight, TFixedAllocator<8>> Heights;
    if (!Surface.HasGround())
    {
        return Heights;
    }
    const FVector FromCentre = Origin - Surface.Centre;
    for (const FVector& Point : FootprintPoints(GearClearanceCm))
    {
        const FVector At = FromCentre + Orientation.RotateVector(Point);
        const double R = At.Size();
        FFootprintHeight& Height = Heights.AddDefaulted_GetRef();
        Height.FromCentre = At;
        Height.Direction = FVector3d(At / R);
        Height.Above = R - Surface.Radius - Surface.Ground->Height(Height.Direction, 0.0);
    }
    return Heights;
}

ShipLanding::FFootprintClearance ShipLanding::FootprintClearance(const FFlightSurface& Surface, const FUniversePosition& Origin,
                                                                 const FQuat& Orientation, double GearClearanceCm)
{
    FFootprintClearance Out;
    const TArray<FFootprintHeight, TFixedAllocator<8>> Heights = FootprintHeights(Surface, Origin, Orientation, GearClearanceCm);
    for (int32 Index = 0; Index < Heights.Num(); ++Index)
    {
        if (Heights[Index].Above < Out.Least)
        {
            Out.Least = Heights[Index].Above;
            Out.Point = Index;
        }
    }
    if (Out.Point != INDEX_NONE)
    {
        Out.GroundNormal = FVector(ShipGround::NormalAt(*Surface.Ground, Heights[Out.Point].Direction));
    }
    return Out;
}
