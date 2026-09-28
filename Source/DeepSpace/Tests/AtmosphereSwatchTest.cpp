#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Sky/ShipSky.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

/**
 * Not a test of correctness but a picture to be judged (atmospheres spec,
 * .GroundSkySwatch; this plan's ruling 1): the ground sky at noon and at
 * dusk, through the shipped law, as the deck's fixed exposure will show it,
 * written to Saved/air_swatch_<sky>_<noon|dusk>.png. It fails only if a
 * swatch cannot be written.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereGroundSkySwatchTest, "DeepSpace.Atmosphere.GroundSkySwatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereStarColourTest, "DeepSpace.Atmosphere.StarColour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereSwatchTestLocal
{
    /** Pixels across: an equidistant fisheye of the whole sky, the zenith at
     *  the centre and the horizon on the rim. */
    constexpr int32 Side = 256;

    /** The dusk sun: the noon sun's azimuth (+X), 3 degrees up. */
    constexpr double DuskSunElevationDeg = 3.0;

    /** The galley's EV100, ds.Sky.Exposure's default (ShipSky.cpp): the
     *  exposure the day sky will be seen at from the deck (decision 9). */
    constexpr double GalleyEV100 = 0.7;

    /** ds.Sky.Radiance's default (ShipSky.cpp): the scene luminance of a
     *  white surface under a Sun at 1 AU. The law's radiance is relative to
     *  a white surface lit head-on by the star, which a temperate world's
     *  star gives it at about that irradiance. */
    constexpr double SkyRadiance = 3.0;

    struct FSky
    {
        FString Name;
        FAirSpec Spec;
        double StarTemperatureK = 0.0;
    };

    /** The skies the developer judges: the spec's fixtures R, G and C by
     *  their stand-ins, until the drawn worlds exist (Task 12). */
    TArray<FSky> Skies()
    {
        using namespace AtmosphereTestFixtures;
        return {
            {TEXT("R_n2o2_2566K"), EarthAir(), HomeStarK},
            {TEXT("G_n2o2_5772K"), EarthAir(), SunK},
            {TEXT("C_co2_2566K"), CarbonDioxide(1.0), HomeStarK}};
    }

    /** A sun ElevationDeg above the horizon toward +X. */
    FVector3d SunAt(double ElevationDeg)
    {
        const double Radians = FMath::DegreesToRadians(ElevationDeg);
        return FVector3d(std::cos(Radians), 0.0, std::sin(Radians));
    }

    /** Straight up from the ground under the noon sun (ruling 4). */
    FVector3d NoonZenith(const FAtmosphere& Air)
    {
        const FVector3d Up(0.0, 0.0, 1.0);
        return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
    }

    /** HSV saturation, 1 - min / max, of the colour with its negative
     *  channels taken as none. */
    double Saturation(const FVector3d& Colour)
    {
        const FVector3d C(FMath::Max(Colour.X, 0.0), FMath::Max(Colour.Y, 0.0), FMath::Max(Colour.Z, 0.0));
        const double Max = C.GetMax();
        return Max > 0.0 ? 1.0 - C.GetMin() / Max : 0.0;
    }

    /** HSV hue, degrees. */
    double Hue(const FVector3d& Colour)
    {
        const FVector3d C(FMath::Max(Colour.X, 0.0), FMath::Max(Colour.Y, 0.0), FMath::Max(Colour.Z, 0.0));
        const double Max = C.GetMax();
        const double Delta = Max - C.GetMin();
        if (Delta <= 0.0)
        {
            return 0.0;
        }
        double Degrees = 0.0;
        if (Max == C.X)
        {
            Degrees = 60.0 * std::fmod((C.Y - C.Z) / Delta, 6.0);
        }
        else if (Max == C.Y)
        {
            Degrees = 60.0 * ((C.Z - C.X) / Delta + 2.0);
        }
        else
        {
            Degrees = 60.0 * ((C.X - C.Y) / Delta + 4.0);
        }
        return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
    }

    /** One fisheye of Air's sky from the ground under Sun, exposed at the
     *  galley's EV, to Saved/air_swatch_<Name>.png. OutPeak is the brightest
     *  channel before exposure. False if the file could not be written. */
    bool Write(const FString& Name, const FAtmosphere& Air, const FVector3d& Sun, FString& OutPath, double& OutPeak)
    {
        const double Scale = SkyRadiance * std::exp2(ShipSky::ManualExposureBias(GalleyEV100));
        const FVector3d Eye(0.0, 0.0, 1.0);
        TArray<FColor> Pixels;
        Pixels.SetNumZeroed(Side * Side);
        OutPeak = 0.0;
        for (int32 Y = 0; Y < Side; ++Y)
        {
            for (int32 X = 0; X < Side; ++X)
            {
                const double U = 2.0 * (X + 0.5) / Side - 1.0;
                const double V = 1.0 - 2.0 * (Y + 0.5) / Side;
                const double Rim = std::sqrt(U * U + V * V);
                if (Rim > 1.0)
                {
                    Pixels[Y * Side + X] = FColor::Black;
                    continue;
                }
                const double FromZenith = Rim * 0.5 * UE_DOUBLE_PI;
                const double Azimuth = std::atan2(V, U);
                const FVector3d Direction(std::sin(FromZenith) * std::cos(Azimuth), std::sin(FromZenith) * std::sin(Azimuth), std::cos(FromZenith));
                const FVector3d Sky = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Eye, Direction, AtmosphereLaw::NoEnd, Sun).InScatter;
                OutPeak = FMath::Max(OutPeak, Sky.GetMax());
                const FLinearColor Scene(static_cast<float>(FMath::Max(Sky.X, 0.0) * Scale),
                                         static_cast<float>(FMath::Max(Sky.Y, 0.0) * Scale),
                                         static_cast<float>(FMath::Max(Sky.Z, 0.0) * Scale));
                // sRGB-encoded and clipped at white, as the display shows it.
                Pixels[Y * Side + X] = Scene.ToFColor(true);
            }
        }
        OutPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), FString::Printf(TEXT("air_swatch_%s.png"), *Name)));
        return FImageUtils::SaveImageByExtension(*OutPath, FImageView(Pixels.GetData(), Side, Side, EGammaSpace::sRGB));
    }
}

bool FAtmosphereGroundSkySwatchTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereSwatchTestLocal;
    const TArray<FSky> All = Skies();
    int32 Written = 0;
    for (const FSky& Sky : All)
    {
        const FAtmosphere Air = FAtmosphere::Build(Sky.Spec, Sky.StarTemperatureK);
        const TPair<const TCHAR*, FVector3d> Times[] = {
            TPair<const TCHAR*, FVector3d>(TEXT("noon"), AtmosphereLaw::NoonSun()),
            TPair<const TCHAR*, FVector3d>(TEXT("dusk"), SunAt(DuskSunElevationDeg))};
        for (const TPair<const TCHAR*, FVector3d>& Time : Times)
        {
            FString Path;
            double Peak = 0.0;
            const bool bWritten = Write(FString::Printf(TEXT("%s_%s"), *Sky.Name, Time.Key), Air, Time.Value, Path, Peak);
            Written += bWritten ? 1 : 0;
            AddInfo(FString::Printf(TEXT("swatch %s at %s (%.0f K, the sun %.0f degrees up): %s; brightest channel %.4f before exposure"),
                *Sky.Name, Time.Key, Sky.StarTemperatureK, FMath::RadiansToDegrees(std::asin(Time.Value.Z)),
                bWritten ? *Path : TEXT("NOT WRITTEN"), Peak));
        }
    }
    TestEqual(TEXT("every swatch is written"), Written, 2 * All.Num());
    return true;
}

bool FAtmosphereStarColourTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereSwatchTestLocal;
    using namespace AtmosphereTestFixtures;
    const auto Zenith = [](double Kelvin) { return NoonZenith(FAtmosphere::Build(EarthAir(), Kelvin, EAtmosphereTable::NoonOnly)); };

    // Decision 3's table, pinned as atmosphere plan ruling 1 restates it: no
    // palette, no floor, no white balance per star. In linear sRGB with a
    // D65 white a red dwarf's sky is peach, the star's own orange pulled
    // toward blue by lambda^-4; the sky is greyest near 3,500 K and blue
    // from 4,000 K up.
    TArray<double> Saturations;
    TArray<int32> Kelvins;
    bool bBlue = true;
    for (int32 Kelvin = 2000; Kelvin <= 15000; Kelvin += 500)
    {
        const FVector3d Sky = Zenith(Kelvin);
        const double S = Saturation(Sky);
        AddInfo(FString::Printf(TEXT("%5d K: noon zenith (%.4f, %.4f, %.4f), saturation %.3f, hue %.0f"),
            Kelvin, Sky.X, Sky.Y, Sky.Z, S, Hue(Sky)));
        Saturations.Add(S);
        Kelvins.Add(Kelvin);
        if (Kelvin >= 4000)
        {
            bBlue &= Hue(Sky) >= 200.0 && Hue(Sky) <= 240.0;
        }
    }
    int32 Least = 0;
    for (int32 I = 1; I < Saturations.Num(); ++I)
    {
        Least = Saturations[I] < Saturations[Least] ? I : Least;
    }
    bool bFalls = true;
    bool bRises = true;
    for (int32 I = 1; I < Saturations.Num(); ++I)
    {
        if (I <= Least)
        {
            bFalls &= Saturations[I] < Saturations[I - 1];
        }
        else
        {
            bRises &= Saturations[I] > Saturations[I - 1];
        }
    }
    // Atmosphere plan ruling 1 (2026-09-27).
    TestTrue(FString::Printf(TEXT("the greyest sky is between 3,000 and 4,500 K (%d K)"), Kelvins[Least]), Kelvins[Least] >= 3000 && Kelvins[Least] <= 4500);
    TestTrue(TEXT("saturation falls from 2,000 K to the greyest and rises from it to 15,000 K"), bFalls && bRises);
    TestTrue(TEXT("from 4,000 K up the sky is blue: hue within 200-240"), bBlue);

    const FVector3d Home = Zenith(HomeStarK);
    const double Coolest = Saturation(Zenith(2000.0));
    const FVector3d Sun = Zenith(SunK);
    // Atmosphere plan ruling 1 (2026-09-27).
    TestTrue(FString::Printf(TEXT("under the home star, 2,566 K, a peach sky: saturation %.3f in [0.62, 0.82]"), Saturation(Home)),
        Saturation(Home) >= 0.62 && Saturation(Home) <= 0.82);
    TestTrue(FString::Printf(TEXT("and its hue %.1f orange, in [15, 40]"), Hue(Home)), Hue(Home) >= 15.0 && Hue(Home) <= 40.0);
    // Atmosphere plan ruling 1 (2026-09-27).
    TestTrue(FString::Printf(TEXT("under 2,000 K deep orange: saturation %.3f >= 0.85"), Coolest), Coolest >= 0.85);
    TestTrue(FString::Printf(TEXT("under the Sun, Earth's blue: hue %.1f in [200, 235]"), Hue(Sun)), Hue(Sun) >= 200.0 && Hue(Sun) <= 235.0);
    TestTrue(FString::Printf(TEXT("and saturation %.3f in [0.40, 0.85]"), Saturation(Sun)), Saturation(Sun) >= 0.40 && Saturation(Sun) <= 0.85);
    return true;
}

#endif
