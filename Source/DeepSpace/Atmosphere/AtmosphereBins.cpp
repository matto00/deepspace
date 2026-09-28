#include "Atmosphere/AtmosphereBins.h"

#include <cmath>

double AtmosphereBins::Weight(const AtmosphereReference::FSpectrum& Star, int32 Index)
{
    return Star.Value[Index] * SkyColour::Spectral::ChannelWeights(Index).Size();
}

AtmosphereBins::FBins AtmosphereBins::Average(const AtmosphereReference::FSpectrum& Star, const AtmosphereReference::FSpectrum& Value)
{
    FBins Out;
    for (int32 Bin = 0; Bin < Count; ++Bin)
    {
        double Sum = 0.0;
        double Total = 0.0;
        for (int32 I = First[Bin]; I < First[Bin + 1]; ++I)
        {
            Sum += Weight(Star, I) * Value.Value[I];
            Total += Weight(Star, I);
        }
        Out.Value[Bin] = Total > 0.0 ? Sum / Total : 0.0;
    }
    return Out;
}

FVector3d AtmosphereBins::Fold(const AtmosphereReference::FSpectrum& Star, int32 Bin)
{
    FVector3d Out = FVector3d::ZeroVector;
    for (int32 I = First[Bin]; I < First[Bin + 1]; ++I)
    {
        Out += SkyColour::Spectral::ChannelWeights(I) * Star.Value[I];
    }
    return Out;
}

FVector3d AtmosphereBins::FoldLight(const AtmosphereReference::FSpectrum& Star, const FBins& Light)
{
    FVector3d Out = FVector3d::ZeroVector;
    for (int32 Bin = 0; Bin < Count; ++Bin)
    {
        Out += Fold(Star, Bin) * Light.Value[Bin];
    }
    return Out;
}

FVector3d AtmosphereBins::FoldThrough(const AtmosphereReference::FSpectrum& Star, const FBins& Through)
{
    // As the .ush's AT_FoldThrough: 1 less what is lost, so nothing lost is
    // exactly 1, even in a channel the star all but lacks.
    const FVector3d Own = AtmosphereReference::ToLinearSrgb(Star);
    const double Floor = FMath::Max(1.0e-4 * FMath::Max3(Own.X, Own.Y, Own.Z), 1.0e-30);
    FBins Lost;
    for (int32 Bin = 0; Bin < Count; ++Bin)
    {
        Lost.Value[Bin] = 1.0 - Through.Value[Bin];
    }
    const FVector3d Gone = FoldLight(Star, Lost);
    return FVector3d(1.0 - Gone.X / FMath::Max(Own.X, Floor), 1.0 - Gone.Y / FMath::Max(Own.Y, Floor), 1.0 - Gone.Z / FMath::Max(Own.Z, Floor));
}
