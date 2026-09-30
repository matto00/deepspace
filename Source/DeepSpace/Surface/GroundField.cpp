#include "Surface/GroundField.h"

FGroundFieldRef ShipGround::FromRelief(const FWorldReliefParams& Params)
{
    return MakeShared<FReliefGround, ESPMode::ThreadSafe>(Params);
}

FVector3d ShipGround::NormalAt(const IGroundField& Ground, const FVector3d& D, double FootprintCm)
{
    FVector3d Grad = FVector3d::ZeroVector;
    Ground.HeightAndGradient(D, Grad, FootprintCm);
    const FVector3d Along = Grad - D * FVector3d::DotProduct(Grad, D);
    return (D - Along / FMath::Max(Ground.RadiusCm(), 1.0)).GetSafeNormal();
}
