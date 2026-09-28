#include "Universe/AirFacts.h"

#include "Universe/UniverseUnits.h"

#include <cmath>

namespace AirFactsLocal
{
    /** Indexed by EAirMix. The aerosol and absorber are decision 5's table:
     *  N2/O2 a thin haze and an ozone-like Chappuis band; CO2 fine
     *  iron-oxide dust that eats blue, as on Mars; H2/He almost clean. */
    const FAirMixFacts Mixes[] = {
        FAirMixFacts{},
        FAirMixFacts{28.97, 1.0, 0.05, 1.2, 1.0, 0.76, 0.95, 0.95, 0.0415},
        FAirMixFacts{44.0, 2.4, 0.08, 2.0, 0.3, 0.70, 0.85, 0.95, 0.0},
        FAirMixFacts{2.3, 0.2, 0.01, 1.0, 1.0, 0.70, 0.99, 0.99, 0.0},
    };
}

const FAirMixFacts& AirFacts::Facts(EAirMix Mix)
{
    return AirFactsLocal::Mixes[static_cast<int32>(Mix)];
}

const TCHAR* AirFacts::Name(EAirMix Mix)
{
    switch (Mix)
    {
    case EAirMix::NitrogenOxygen: return TEXT("nitrogen-oxygen");
    case EAirMix::CarbonDioxide: return TEXT("carbon-dioxide");
    case EAirMix::HydrogenHelium: return TEXT("hydrogen-helium");
    default: return TEXT("none");
    }
}

double AirFacts::ScaleHeightKm(EAirMix Mix, double TemperatureK, double GravityEarth)
{
    const double Mu = Facts(Mix).MeanMolecularWeight;
    if (!(Mu > 0.0 && TemperatureK > 0.0 && GravityEarth > 0.0))
    {
        return 0.0;
    }
    return BoltzmannJPerK * TemperatureK / (Mu * AtomicMassKg * GravityEarth * StandardGravityMS2) / 1000.0;
}

double AirFacts::ColumnRelativeToEarth(EAirMix Mix, double PressureBar, double GravityEarth)
{
    const double Mu = Facts(Mix).MeanMolecularWeight;
    if (!(Mu > 0.0 && GravityEarth > 0.0))
    {
        return 0.0;
    }
    return PressureBar / GravityEarth * (EarthAirMolecularWeight / Mu);
}

double AirFacts::RayleighTau550(EAirMix Mix, double PressureBar, double GravityEarth)
{
    return EarthRayleighTau550PerBar * ColumnRelativeToEarth(Mix, PressureBar, GravityEarth) * Facts(Mix).RayleighPerAir;
}

double AirFacts::AerosolTau550(EAirMix Mix, double PressureBar, double GravityEarth)
{
    return GravityEarth > 0.0 ? Facts(Mix).AerosolTau550PerBar * PressureBar / GravityEarth : 0.0;
}

double AirFacts::OzoneTau600(EAirMix Mix, double PressureBar, double GravityEarth)
{
    return GravityEarth > 0.0 ? Facts(Mix).OzoneTau600PerBar * PressureBar / GravityEarth : 0.0;
}

double AirFacts::NadirTau450(EAirMix Mix, double PressureBar, double GravityEarth)
{
    const double Rayleigh = RayleighTau550(Mix, PressureBar, GravityEarth) * std::pow(550.0 / 450.0, 4.0);
    const double Aerosol = AerosolTau550(Mix, PressureBar, GravityEarth) * std::pow(450.0 / 550.0, -Facts(Mix).AerosolAngstrom);
    return Rayleigh + Aerosol;
}

double AirFacts::JeansLambda(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK)
{
    const double Mu = Facts(Mix).MeanMolecularWeight;
    if (!(Mu > 0.0 && MassEarth > 0.0 && RadiusEarth > 0.0 && EquilibriumK > 0.0))
    {
        return 0.0;
    }
    const double RadiusM = RadiusEarth * UniverseUnits::CmPerEarthRadius / 100.0;
    const double GravityMS2 = StandardGravityMS2 * MassEarth / (RadiusEarth * RadiusEarth);
    const double Escape = std::sqrt(2.0 * GravityMS2 * RadiusM);
    const double Exobase = GenGuarantees::ExobaseFactor * EquilibriumK;
    const double Rms = std::sqrt(3.0 * BoltzmannJPerK * Exobase / (Mu * AtomicMassKg));
    return Escape / Rms;
}

double AirFacts::Retention(EAirMix Mix, double MassEarth, double RadiusEarth, double EquilibriumK)
{
    if (Mix == EAirMix::None)
    {
        return 0.0;
    }
    const double Lambda = JeansLambda(Mix, MassEarth, RadiusEarth, EquilibriumK);
    const double T = FMath::Clamp((Lambda - GenGuarantees::LostBelow) / (GenGuarantees::RetainedAbove - GenGuarantees::LostBelow), 0.0, 1.0);
    return T * T * (3.0 - 2.0 * T);
}

double AirFacts::PressureCeilingBar(EAirMix Mix, double GravityEarth, double RetentionOfMix)
{
    const double PerBar = NadirTau450(Mix, 1.0, GravityEarth);
    return PerBar > 0.0 ? FMath::Max(RetentionOfMix, 0.0) * GenGuarantees::MaxNadirTau450 / PerBar : 0.0;
}

double AirFacts::SmoothCeiling(double DrawnBar, double CeilingBar)
{
    if (!(DrawnBar > 0.0 && CeilingBar > 0.0))
    {
        return 0.0;
    }
    // 1 / (D^-4 + C^-4)^(1/4), written as D / (1 + (D / C)^4)^(1/4) so no
    // small draw raises anything to a huge power.
    return DrawnBar / std::pow(1.0 + std::pow(DrawnBar / CeilingBar, 4.0), 0.25);
}

double AirFacts::GiantDiscPressureBar(double GravityEarth)
{
    return PressureCeilingBar(EAirMix::HydrogenHelium, GravityEarth, 1.0);
}
