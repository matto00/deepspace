#include "Sky/SkyStarfield.h"
#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"

namespace
{
    // Stream labels, one per quantity (procgen decision 2).
    constexpr uint64 DirectionLabel = GenSeed::Label("sky.starfield.direction");
    constexpr uint64 FluxLabel = GenSeed::Label("sky.starfield.flux");
    constexpr uint64 TemperatureLabel = GenSeed::Label("sky.starfield.temperature");

    /** Most naked-eye stars are in the disc: nearby disc stars dominate what
     *  a planet's sky shows, the rest are too close to be anywhere but all
     *  around. */
    constexpr double DiscFraction = 0.65;

    /** The disc's thickness as seen from inside it, rad. Laplace rather than
     *  normal because a disc seen edge-on is sharply peaked with long wings:
     *  density falls exponentially with height above the plane. */
    constexpr double DiscLatitudeScale = 0.18;

    /**
     * Toward the galactic centre the band is richer. A von Mises of
     * concentration 1 around the centre, drawn as the wrapped normal with the
     * same mean resultant length -- sigma = sqrt(-2 ln(I1(1) / I0(1))) =
     * 1.27 rad -- which at this spread no eye can tell apart.
     */
    constexpr double DiscLongitudeSigma = 1.27;

    /** Star counts to a flux limit go as F^-3/2 for stars scattered evenly
     *  through space -- the Euclidean count law, not a taste. */
    constexpr double FluxAlpha = 1.5;

    /** Log-normal: a temperature is set by mass, which the initial mass
     *  function makes a magnitude, and a naked-eye sky over-represents the
     *  luminous hot ones against the cool dwarfs that outnumber them. Median
     *  5,000 K, a factor of e^0.3 either way at one sigma. */
    constexpr double TemperatureMedian = 5000.0;
    constexpr double TemperatureSigma = 0.3;

    FVector DrawDirection(FGenStream& Stream)
    {
        double Latitude;
        double Longitude;
        if (Stream.Chance(DiscFraction))
        {
            // Laplace: an exponential height with a random side.
            const double Height = Stream.Exponential(DiscLatitudeScale);
            Latitude = Stream.Chance(0.5) ? Height : -Height;
            Longitude = Stream.Normal(0.0, DiscLongitudeSigma);

            // A tail past the pole carries on over it, down the far side.
            if (FMath::Abs(Latitude) > 0.5 * UE_DOUBLE_PI)
            {
                Latitude = FMath::Sign(Latitude) * (UE_DOUBLE_PI - FMath::Abs(Latitude));
                Longitude += UE_DOUBLE_PI;
            }
        }
        else
        {
            // The halo has no preferred direction, which on a sphere means
            // uniform in the sine of latitude, not in latitude -- uniform
            // latitude would crowd the poles. The one place uniform is true.
            Latitude = FMath::Asin(2.0 * Stream.Unit() - 1.0);
            Longitude = 2.0 * UE_DOUBLE_PI * Stream.Unit();
        }
        const double CosLatitude = FMath::Cos(Latitude);
        return FVector(CosLatitude * FMath::Cos(Longitude), CosLatitude * FMath::Sin(Longitude), FMath::Sin(Latitude));
    }
}

TArray<FSkyStar> SkyStarfield::Generate(uint64 Seed, int32 Count)
{
    TArray<FSkyStar> Stars;
    Stars.Reserve(FMath::Max(Count, 0));
    for (int32 Index = 0; Index < Count; ++Index)
    {
        FSkyStar& Star = Stars.AddDefaulted_GetRef();

        FGenStream Direction(GenSeed::Derive(Seed, DirectionLabel, static_cast<uint64>(Index)));
        Star.Direction = DrawDirection(Direction);

        FGenStream Flux(GenSeed::Derive(Seed, FluxLabel, static_cast<uint64>(Index)));
        Star.Flux = Flux.ParetoBounded(FluxAlpha, 1.0, MaxFlux);

        FGenStream Temperature(GenSeed::Derive(Seed, TemperatureLabel, static_cast<uint64>(Index)));
        Star.TemperatureK = Temperature.LogNormalBounded(TemperatureMedian, TemperatureSigma, MinTemperatureK, MaxTemperatureK);
    }
    return Stars;
}
