#include "Atmosphere/PlanetAir.h"

#include "Atmosphere/Atmosphere.h"
#include "Universe/AirFacts.h"
#include "Universe/UniverseUnits.h"

FAirSpec PlanetAir::SpecOf(const FPlanet& Planet)
{
    FAirSpec Spec;
    Spec.RadiusCm = Planet.RadiusEarth * UniverseUnits::CmPerEarthRadius;
    if (Planet.AirMix == EAirMix::None || !(Planet.SurfacePressureBar > 0.0))
    {
        return Spec;
    }
    const double Gravity = Planet.SurfaceGravityEarth();
    const FAirMixFacts& Facts = AirFacts::Facts(Planet.AirMix);
    Spec.GasScaleHeightCm = AirFacts::ScaleHeightKm(Planet.AirMix, Planet.EquilibriumK, Gravity) * UniverseUnits::CmPerKm;
    Spec.GasTau550 = AirFacts::RayleighTau550(Planet.AirMix, Planet.SurfacePressureBar, Gravity);
    Spec.OzoneTau600 = AirFacts::OzoneTau600(Planet.AirMix, Planet.SurfacePressureBar, Gravity);
    Spec.AerosolScaleHeightCm = Facts.AerosolScaleHeightKm * UniverseUnits::CmPerKm;
    Spec.AerosolTau550 = AirFacts::AerosolTau550(Planet.AirMix, Planet.SurfacePressureBar, Gravity);
    Spec.AerosolAngstrom = Facts.AerosolAngstrom;
    Spec.AerosolAsymmetry = Facts.AerosolAsymmetry;
    Spec.AerosolAlbedo450 = Facts.AerosolAlbedo450;
    Spec.AerosolAlbedo650 = Facts.AerosolAlbedo650;
    return Spec;
}

FVector3d PlanetAir::NoonZenith(const FPlanet& Planet, double StarTemperatureK)
{
    // Only the two table columns the noon zenith reads: exactly the full
    // table's there, at a sixteenth of the cost -- the corpus asks this of
    // thousands of worlds.
    const FAtmosphere Air = FAtmosphere::Build(SpecOf(Planet), StarTemperatureK, EAtmosphereTable::NoonOnly);
    if (!Air.HasAir())
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d Up(0.0, 0.0, 1.0);
    return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
}

double PlanetAir::Saturation(const FVector3d& Colour)
{
    const FVector3d C(FMath::Max(Colour.X, 0.0), FMath::Max(Colour.Y, 0.0), FMath::Max(Colour.Z, 0.0));
    const double Max = C.GetMax();
    return Max > 0.0 ? 1.0 - C.GetMin() / Max : 0.0;
}
