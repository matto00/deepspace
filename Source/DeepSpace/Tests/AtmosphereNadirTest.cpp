#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/PlanetAir.h"
#include "Universe/AirFacts.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmospherePlanetAirTest, "DeepSpace.Atmosphere.PlanetAir",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereNadirLegibleTest, "DeepSpace.Atmosphere.NadirLegible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereNadirTestLocal
{
    constexpr double SunK = 5772.0;

    FPlanet Made(EPlanetKind Kind, double MassEarth, double RadiusEarth, double EquilibriumK, EAirMix Mix, double PressureBar)
    {
        FPlanet Planet;
        Planet.Designation = TEXT("Made");
        Planet.Kind = Kind;
        Planet.MassEarth = MassEarth;
        Planet.RadiusEarth = RadiusEarth;
        Planet.EquilibriumK = EquilibriumK;
        Planet.AirMix = Mix;
        Planet.SurfacePressureBar = PressureBar;
        return Planet;
    }

    /** The least, over the channels, of the contrast between an albedo-0.3
     *  and an albedo-0.1 surface under an overhead star, seen from 400 km
     *  straight down, as a fraction of the airless contrast: the sunlight
     *  that reaches the ground times the view's transmittance. The air's
     *  own light adds equally to both and cancels. */
    double NadirContrast(const FPlanet& Planet)
    {
        const FAirSpec Spec = PlanetAir::SpecOf(Planet);
        // No table: multiple scattering enters only the air's own light,
        // which cancels below, and never the sunlight or the view's
        // transmittance, so the contrast is the full table's -- at none of
        // its cost (twelve full tables were 16 s, over the suite's 5 s cap).
        const FAtmosphere Air = FAtmosphere::Build(Spec, SunK, EAtmosphereTable::None);
        const double Altitude = 4.0e7 / Spec.RadiusCm;
        const FVector3d Up(0.0, 0.0, 1.0);
        const FAtmosphereScatter View = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up * (1.0 + Altitude), -Up, Altitude, Up);
        const FVector3d Sun = AtmosphereLaw::SunThroughF64(Air.GetAir(), Up, Up);
        const double Bright = 0.3;
        const double Dark = 0.1;
        double Least = 1.0;
        for (int32 C = 0; C < 3; ++C)
        {
            const double Light = Bright * Sun[C] * View.Transmittance[C] + View.InScatter[C];
            const double Shade = Dark * Sun[C] * View.Transmittance[C] + View.InScatter[C];
            Least = FMath::Min(Least, (Light - Shade) / (Bright - Dark));
        }
        return Least;
    }
}

bool FAtmospherePlanetAirTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereNadirTestLocal;

    const FPlanet Earth = Made(EPlanetKind::Terrestrial, 1.0, 1.0, 255.0, EAirMix::NitrogenOxygen, 1.0);
    const FAirSpec Spec = PlanetAir::SpecOf(Earth);
    TestTrue(TEXT("an Earth has air"), Spec.HasAir());
    TestEqual(TEXT("its radius is the world's"), Spec.RadiusCm, UniverseUnits::CmPerEarthRadius);
    TestTrue(TEXT("its gas's scale height is AirFacts'"),
        FMath::IsNearlyEqual(Spec.GasScaleHeightCm, AirFacts::ScaleHeightKm(EAirMix::NitrogenOxygen, 255.0, 1.0) * UniverseUnits::CmPerKm));
    TestTrue(TEXT("its Rayleigh depth is Earth's 0.097"), FMath::IsNearlyEqual(Spec.GasTau550, 0.097, 1.0e-12));
    TestTrue(TEXT("its ozone 0.0415 and its haze 0.05"), FMath::IsNearlyEqual(Spec.OzoneTau600, 0.0415, 1.0e-12) && FMath::IsNearlyEqual(Spec.AerosolTau550, 0.05, 1.0e-12));
    TestTrue(TEXT("its haze's shape is the mix's"), FMath::IsNearlyEqual(Spec.AerosolScaleHeightCm, 1.2e5, 1.0e-6) && Spec.AerosolAsymmetry == 0.76 && Spec.AerosolAlbedo450 == 0.95);

    const FPlanet Rock = Made(EPlanetKind::Barren, 1.0, 1.0, 400.0, EAirMix::None, 0.0);
    TestFalse(TEXT("a barren world has no air"), PlanetAir::SpecOf(Rock).HasAir());
    TestTrue(TEXT("and no sky"), PlanetAir::NoonZenith(Rock, SunK).IsZero());

    const double JupiterG = 318.0 / 121.0;
    const FPlanet Jupiter = Made(EPlanetKind::GasGiant, 318.0, 11.0, 110.0, EAirMix::HydrogenHelium, AirFacts::GiantDiscPressureBar(JupiterG));
    const FAirSpec Giant = PlanetAir::SpecOf(Jupiter);
    const double Tau450 = Giant.GasTau550 * std::pow(550.0 / 450.0, 4.0) + Giant.AerosolTau550 * std::pow(450.0 / 550.0, -Giant.AerosolAngstrom);
    TestTrue(FString::Printf(TEXT("a Jupiter's disc is where straight down at 450 nm reaches the guarantee, %.3f (%.6f)"), GenGuarantees::MaxNadirTau450, Tau450),
        FMath::Abs(Tau450 - GenGuarantees::MaxNadirTau450) < 1.0e-9);

    const FVector3d Sky = PlanetAir::NoonZenith(Earth, SunK);
    TestTrue(FString::Printf(TEXT("an Earth's noon zenith under the Sun is blue (%.4f, %.4f, %.4f)"), Sky.X, Sky.Y, Sky.Z), Sky.Z > Sky.X);
    const double S = PlanetAir::Saturation(Sky);
    TestTrue(FString::Printf(TEXT("its saturation %.3f is a number between 0 and 1"), S), S > 0.0 && S < 1.0);
    TestEqual(TEXT("black has no saturation"), PlanetAir::Saturation(FVector3d::ZeroVector), 0.0);
    return true;
}

bool FAtmosphereNadirLegibleTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereNadirTestLocal;
    const EAirMix Mixes[] = {EAirMix::NitrogenOxygen, EAirMix::CarbonDioxide, EAirMix::HydrogenHelium};
    TArray<FPlanet> Worlds;
    for (const EAirMix Mix : Mixes)
    {
        for (const double G : {0.6, 1.0, 2.0})
        {
            // Mass g at radius 1: AirFacts reads the gravity, not the size.
            Worlds.Add(Made(EPlanetKind::Terrestrial, G, 1.0, 255.0, Mix, AirFacts::PressureCeilingBar(Mix, G, 1.0)));
        }
    }
    for (const double Mass : {15.0, 318.0, 3000.0})
    {
        const double G = Mass / 121.0;
        Worlds.Add(Made(EPlanetKind::GasGiant, Mass, 11.0, 110.0, EAirMix::HydrogenHelium, AirFacts::GiantDiscPressureBar(G)));
    }

    for (const FPlanet& Candidate : Worlds)
    {
        const double Contrast = NadirContrast(Candidate);
        const double G = Candidate.SurfaceGravityEarth();
        FString Diagnosis;
        if (Contrast < 0.5)
        {
            // Where this world would pass: the pressure fraction that brings
            // its worst channel to half, by bisection, as a depth at 450 nm.
            double Low = 0.0;
            double High = 1.0;
            for (int32 Step = 0; Step < 30; ++Step)
            {
                const double Mid = 0.5 * (Low + High);
                FPlanet Thinner = Candidate;
                Thinner.SurfacePressureBar = Candidate.SurfacePressureBar * Mid;
                (NadirContrast(Thinner) >= 0.5 ? Low : High) = Mid;
            }
            Diagnosis = FString::Printf(TEXT(" -- it would reach half at %.3f of its pressure, tau450 %.3f"),
                Low, AirFacts::NadirTau450(Candidate.AirMix, Candidate.SurfacePressureBar * Low, G));
        }
        AddInfo(FString::Printf(TEXT("%s at %.2f g, %.3f bar (tau450 %.3f): nadir contrast %.3f of airless%s"),
            AirFacts::Name(Candidate.AirMix), G, Candidate.SurfacePressureBar, AirFacts::NadirTau450(Candidate.AirMix, Candidate.SurfacePressureBar, G), Contrast, *Diagnosis));
        TestTrue(FString::Printf(TEXT("%s at %.2f g keeps half its surface's contrast straight down"), AirFacts::Name(Candidate.AirMix), G), Contrast >= 0.5);
    }
    return true;
}

#endif
