#include "Universe/SystemDescription.h"

#include "Universe/UniverseUnits.h"

namespace
{
    FString SectorText(const FSystemId& Id)
    {
        return FString::Printf(TEXT("sector (%lld, %lld, %lld) slot %d"),
            static_cast<long long>(Id.Sector.X), static_cast<long long>(Id.Sector.Y),
            static_cast<long long>(Id.Sector.Z), Id.Slot);
    }

    /** People, rounded as a person would say it. */
    FString PopulationText(double Population)
    {
        if (Population >= 1.0e6)
        {
            return FString::Printf(TEXT("%.1f million people"), Population / 1.0e6);
        }
        if (Population >= 1.0e4)
        {
            return FString::Printf(TEXT("%.0f thousand people"), Population / 1.0e3);
        }
        return FString::Printf(TEXT("%.0f people"), Population);
    }
}

const TCHAR* SystemDescription::ClassName(EStarClass Class)
{
    switch (Class)
    {
    case EStarClass::M: return TEXT("M");
    case EStarClass::K: return TEXT("K");
    case EStarClass::G: return TEXT("G");
    case EStarClass::F: return TEXT("F");
    case EStarClass::A: return TEXT("A");
    case EStarClass::B: return TEXT("B");
    }
    return TEXT("?");
}

const TCHAR* SystemDescription::KindName(EPlanetKind Kind)
{
    switch (Kind)
    {
    case EPlanetKind::Barren: return TEXT("barren");
    case EPlanetKind::Terrestrial: return TEXT("terrestrial");
    case EPlanetKind::Ocean: return TEXT("ocean");
    case EPlanetKind::Ice: return TEXT("ice");
    case EPlanetKind::GasGiant: return TEXT("gas giant");
    }
    return TEXT("?");
}

FString SystemDescription::DescribeStub(const FStarSystemStub& Stub, double DistanceCm)
{
    FString Line = FString::Printf(TEXT("%-12s %s  L %.3g  %.0f K"),
        *Stub.Name, ClassName(Stub.Class), Stub.LuminositySolar, Stub.TemperatureK);
    if (DistanceCm >= 0.0)
    {
        Line += FString::Printf(TEXT("  %6.2f ly"), DistanceCm / UniverseUnits::CmPerLightYear);
    }
    Line += TEXT("  ") + SectorText(Stub.Id);
    return Line;
}

FString SystemDescription::Describe(const FStarSystem& System)
{
    const FStar& Star = System.Star;

    FString Text = FString::Printf(TEXT("%s -- %s star, %.2f Msun, %.2f Rsun, L %.3g, %.0f K  (%s, seed 0x%016llX)\n"),
        *System.Stub.Name, ClassName(Star.Class), Star.MassSolar, Star.RadiusSolar, Star.LuminositySolar,
        Star.TemperatureK, *SectorText(System.Stub.Id), static_cast<unsigned long long>(System.Stub.Seed));
    Text += FString::Printf(TEXT("  habitable %.3f-%.3f AU, frost line %.3f AU\n"),
        Star.HabitableInnerAU, Star.HabitableOuterAU, Star.FrostLineAU);

    if (System.Planets.IsEmpty())
    {
        Text += TEXT("  no planets\n");
        return Text;
    }

    for (const FPlanet& Planet : System.Planets)
    {
        Text += FString::Printf(TEXT("  %-14s %-11s %8.3f AU  %8.2f Mearth  %5.2f Rearth  %5.0f K"),
            *Planet.Designation, KindName(Planet.Kind), Planet.SemiMajorAxisAU, Planet.MassEarth,
            Planet.RadiusEarth, Planet.EquilibriumK);
        if (Planet.Population > 0.0)
        {
            Text += FString::Printf(TEXT("  -- %s, %s"), *Planet.GivenName, *PopulationText(Planet.Population));
        }
        Text += TEXT("\n");
    }
    return Text;
}
