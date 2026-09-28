#include "Ship/ShipGravity.h"

FVector ShipFlight::GravityAt(TConstArrayView<FGravityWell> Wells, const FUniversePosition& Position)
{
    FVector Sum = FVector::ZeroVector;
    for (const FGravityWell& Well : Wells)
    {
        const FVector ToCentre = Well.Centre - Position;
        const double R = ToCentre.Size();
        if (!(R > 0.0) || !(Well.Mu > 0.0))
        {
            continue;
        }
        const double Inside = FMath::Max(Well.RadiusCm, 0.0);
        const double Pull = R >= Inside ? Well.Mu / (R * R) : Well.Mu * R / (Inside * Inside * Inside);
        Sum += ToCentre * (Pull / R);
    }
    return Sum;
}
