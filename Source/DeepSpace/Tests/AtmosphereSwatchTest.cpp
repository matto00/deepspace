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

#endif
