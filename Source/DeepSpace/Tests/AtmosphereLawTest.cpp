#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereAirlessIsZeroTest, "DeepSpace.Atmosphere.AirlessIsZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereLimbBeyondSilhouetteTest, "DeepSpace.Atmosphere.LimbBeyondSilhouette",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereTerminatorReddensTest, "DeepSpace.Atmosphere.TerminatorReddens",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereCrescentAtHighPhaseTest, "DeepSpace.Atmosphere.CrescentAtHighPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereBacklitRingTest, "DeepSpace.Atmosphere.BacklitRing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereImpactParameterTest, "DeepSpace.Atmosphere.ImpactParameter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereEyeBelowTheDatumTest, "DeepSpace.Atmosphere.EyeBelowTheDatum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereNoStarTest, "DeepSpace.Atmosphere.NoStar",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereDegenerateRaysTest, "DeepSpace.Atmosphere.DegenerateRays",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereLawTestLocal
{
    using AtmosphereLaw::NoEnd;

    double Luminance(const FVector3d& Colour)
    {
        return 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
    }

    bool IsFinite(const FVector3d& V)
    {
        return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
    }

    const FAtmosphere& Earth()
    {
        static const FAtmosphere Air = FAtmosphere::Build(AtmosphereTestFixtures::EarthAir(), AtmosphereTestFixtures::SunK);
        return Air;
    }

    FAtmosphereScatter Look(const FAtmosphere& Air, const FVector3d& Eye, const FVector3d& Direction, double Length, const FVector3d& Sun)
    {
        return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Eye, Direction, Length, Sun);
    }

    FAtmosphereScatter Look32(const FAtmosphere& Air, const FVector3d& Eye, const FVector3d& Direction, double Length, const FVector3d& Sun)
    {
        return AtmosphereLaw::InScatterF32(Air.GetAir(), Air.GetTable(), FVector3f(Eye), FVector3f(Direction), static_cast<float>(Length), FVector3f(Sun));
    }

    /** A ray from 7.8 radii travelling +Y that grazes the air Height radii
     *  above the surface, on the +X side (Side 1) or the -X side (Side -1). */
    FAtmosphereScatter Limb(const FAtmosphere& Air, double Height, const FVector3d& Sun, double Side = 1.0)
    {
        return Look(Air, FVector3d(Side * (1.0 + Height), -7.8, 0.0), FVector3d(0.0, 1.0, 0.0), NoEnd, Sun);
    }

    /** At the surface point (1, 0, 0), a sun Degrees above the horizon. */
    FVector3d SunAtElevation(double Degrees)
    {
        const double Radians = FMath::DegreesToRadians(Degrees);
        return FVector3d(std::sin(Radians), std::cos(Radians), 0.0);
    }
}

bool FAtmosphereAirlessIsZeroTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    const FAtmosphere None = FAtmosphere::Build(AtmosphereTestFixtures::Airless(), AtmosphereTestFixtures::SunK);
    const FVector3d Up(0.0, 0.0, 1.0);
    for (const FAtmosphereScatter& S : {Look(None, FVector3d(0.0, 0.0, 1.001), Up, NoEnd, Up), Look32(None, FVector3d(0.0, 0.0, 1.001), Up, NoEnd, Up),
                                        Look(None, FVector3d(1.001, -7.8, 0.0), FVector3d(0.0, 1.0, 0.0), NoEnd, FVector3d(1.0, 0.0, 0.0))})
    {
        TestTrue(TEXT("an airless world adds exactly nothing"), S.InScatter == FVector3d::ZeroVector);
        TestTrue(TEXT("and dims exactly nothing"), S.Transmittance == FVector3d::OneVector);
    }
    TestTrue(TEXT("its sunlight passes whole, the night side too: the disc's Lambert darkens it, not an air"),
        AtmosphereLaw::SunThroughF64(None.GetAir(), FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, -1.0)) == FVector3d::OneVector);
    TestTrue(TEXT("and with no star"), AtmosphereLaw::SunThroughF64(None.GetAir(), FVector3d(0.0, 0.0, 1.0), FVector3d::ZeroVector) == FVector3d::OneVector);
    TestTrue(TEXT("its transmittance is one"), AtmosphereLaw::TransmittanceF32(None.GetAir(), FVector3f(0.0f, 0.0f, 3.0f), FVector3f(0.0f, 0.0f, -1.0f), 2.0f) == FVector3d::OneVector);
    return true;
}

bool FAtmosphereLimbBeyondSilhouetteTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    const FAtmosphereAir& A = Earth().GetAir();
    const FVector3d Noon(1.0, 0.0, 0.0);
    const FAtmosphereScatter Low = Limb(Earth(), 2.0 * A.GasH, Noon);
    const FAtmosphereScatter High = Limb(Earth(), 8.0 * A.GasH, Noon);
    const FAtmosphereScatter Above = Limb(Earth(), A.Top + 1.0e-4, Noon);
    AddInfo(FString::Printf(TEXT("the lit limb at 2 H: (%.5f, %.5f, %.5f); at 8 H luminance %.3e"),
        Low.InScatter.X, Low.InScatter.Y, Low.InScatter.Z, Luminance(High.InScatter)));
    TestTrue(TEXT("the air shows past the silhouette: a ray grazing 2 H up is lit"), Luminance(Low.InScatter) > 1.0e-4);
    TestTrue(TEXT("and fades with height"), Luminance(Low.InScatter) > Luminance(High.InScatter) && Luminance(High.InScatter) > 0.0);
    TestTrue(TEXT("a ray above the air's top meets none of it"),
        Above.InScatter == FVector3d::ZeroVector && Above.Transmittance == FVector3d::OneVector);
    TestTrue(TEXT("the grazing path reddens what passes through it"), Low.Transmittance.X > Low.Transmittance.Z);
    return true;
}

bool FAtmosphereTerminatorReddensTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    const FAtmosphereAir& A = Earth().GetAir();
    const FVector3d Surface(1.0, 0.0, 0.0);
    const FVector3d Low = AtmosphereLaw::SunThroughF64(A, Surface, SunAtElevation(2.0));
    const FVector3d High = AtmosphereLaw::SunThroughF64(A, Surface, SunAtElevation(60.0));
    AddInfo(FString::Printf(TEXT("the ground's sunlight at 2 degrees (%.4f, %.4f, %.4f), at 60 (%.4f, %.4f, %.4f)"),
        Low.X, Low.Y, Low.Z, High.X, High.Y, High.Z));
    TestTrue(TEXT("near the terminator the ground is lit red over blue"), Low.X > Low.Z);
    TestTrue(TEXT("and far redder than under a high sun"), Low.X / Low.Z > 2.0 * High.X / High.Z);
    TestTrue(TEXT("a high sun reaches the ground mostly unreddened"), High.Z > 0.5);
    TestTrue(TEXT("past the terminator the ground is in its own shadow"),
        AtmosphereLaw::SunThroughF64(A, Surface, SunAtElevation(-0.5)) == FVector3d::ZeroVector);

    // Twilight: seen from 7.8 radii, the air over a point one degree into
    // the night is still lit from above the shadow.
    const FVector3d Eye(7.8, 0.0, 0.0);
    const double OneDegree = FMath::DegreesToRadians(1.0);
    const FVector3d Night(std::cos(OneDegree), -std::sin(OneDegree), 0.0);
    const FVector3d ToNight = Night - Eye;
    const FAtmosphereScatter Twilight = Look(Earth(), Eye, ToNight.GetSafeNormal(), ToNight.Size(), FVector3d(0.0, 1.0, 0.0));
    TestTrue(FString::Printf(TEXT("the night side's first degree glows (luminance %.3e)"), Luminance(Twilight.InScatter)),
        Luminance(Twilight.InScatter) > 0.0);
    return true;
}

bool FAtmosphereCrescentAtHighPhaseTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    const double H = Earth().GetAir().GasH;
    // Phase 150 degrees: the star mostly behind the world, off to +X.
    const FVector3d Behind(0.5, 0.8660254037844386, 0.0);
    const double SunSide = Luminance(Limb(Earth(), 2.0 * H, Behind, 1.0).InScatter);
    const double FarSide = Luminance(Limb(Earth(), 2.0 * H, Behind, -1.0).InScatter);
    const double Quarter = Luminance(Limb(Earth(), 2.0 * H, FVector3d(1.0, 0.0, 0.0), 1.0).InScatter);
    AddInfo(FString::Printf(TEXT("phase 150: the sun's side %.4e, the far side %.4e; phase 90: %.4e"), SunSide, FarSide, Quarter));
    TestTrue(TEXT("at high phase the limb is a crescent: the sun's side outshines the far side five times"), SunSide > 5.0 * FarSide);
    TestTrue(TEXT("forward scattering: the crescent outshines the lit limb at quarter phase"), SunSide > Quarter);
    return true;
}

bool FAtmosphereBacklitRingTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    const double H = Earth().GetAir().GasH;
    // The star exactly behind the world: the eye, the world and the star on
    // one line. Compared at one grazing height, 3 H, where forward
    // scattering wins: lower, the ring's light crosses the whole grazing
    // path twice over and is its own extinction (planning note 10).
    const FVector3d Behind(0.0, 1.0, 0.0);
    const FVector3d Quarter(1.0, 0.0, 0.0);
    const double Ring = Luminance(Limb(Earth(), 3.0 * H, Behind).InScatter);
    const double Lit = Luminance(Limb(Earth(), 3.0 * H, Quarter).InScatter);
    AddInfo(FString::Printf(TEXT("3 H up: the backlit ring %.4e, the lit limb at phase 90 %.4e"), Ring, Lit));
    TestTrue(TEXT("with the star behind the world the ring outshines the lit limb at 90 degrees of phase, at the same height"), Ring > Lit);

    // The brightest of each, for the developer: the lit limb's peak sits
    // lower, in air the ring cannot shine through.
    double RingPeak = 0.0;
    double LitPeak = 0.0;
    for (double Height = 0.25; Height <= 8.0; Height += 0.25)
    {
        RingPeak = FMath::Max(RingPeak, Luminance(Limb(Earth(), Height * H, Behind).InScatter));
        LitPeak = FMath::Max(LitPeak, Luminance(Limb(Earth(), Height * H, Quarter).InScatter));
    }
    AddInfo(FString::Printf(TEXT("brightest over 0.25-8 H: the ring %.4e, the lit limb %.4e"), RingPeak, LitPeak));

    // The ring is the haze's forward peak, not only the gas's. At 3 H in
    // Earth's air there is no haze left (its scale height is 1.2 km), so the
    // gas's (1 + cos^2) alone makes the ring win there. Low in a thin air,
    // where the grazing path is still optically thin and the haze is dense,
    // the aerosol's g carries the ring: the gas alone could make it at most
    // twice the lit limb, and the haze makes it many times that.
    const FAtmosphere Thin = FAtmosphere::Build(AtmosphereTestFixtures::NitrogenOxygen(AtmosphereTestFixtures::LowBar), AtmosphereTestFixtures::SunK);
    const double ThinH = Thin.GetAir().GasH;
    const double HazyRing = Luminance(Limb(Thin, 0.25 * ThinH, Behind).InScatter);
    const double HazyLit = Luminance(Limb(Thin, 0.25 * ThinH, Quarter).InScatter);
    AddInfo(FString::Printf(TEXT("0.05 bar, 0.25 H up: the backlit ring %.4e, the lit limb %.4e, %.1f times"), HazyRing, HazyLit, HazyRing / FMath::Max(HazyLit, 1.0e-300)));
    TestTrue(TEXT("low in a thin hazy air the ring outshines the lit limb by more than the gas's twofold could: the haze scatters forward"), HazyRing > 4.0 * HazyLit);
    return true;
}

bool FAtmosphereImpactParameterTest::RunTest(const FString& Parameters)
{
    // A limb ray from a thousand radii, in no special axes: the float impact
    // parameter must be the double one's, which the naive |E|^2 - (E.D)^2 or
    // an unguarded cross product loses at this range.
    const FQuat Turn(FVector(0.3, 0.5, 0.8).GetSafeNormal(), 0.7);
    const double B = 1.0 + 2.0 * 7.463e5 / AtmosphereTestFixtures::EarthRadiusCm;
    const FVector3f Eye(Turn.RotateVector(FVector(B, -1000.0, 0.0)));
    const FVector3f Direction(Turn.RotateVector(FVector(0.0, 1.0, 0.0)));
    const double Double = AtmosphereLaw::ImpactParameterF64(FVector3d(Eye), FVector3d(Direction));
    const float Float = AtmosphereLaw::ImpactParameterF32(Eye, Direction);
    AddInfo(FString::Printf(TEXT("impact parameter from 1,000 radii: double %.9f, float %.9f"), Double, static_cast<double>(Float)));
    TestTrue(TEXT("the double sees the tangent 2 H up, to the inputs' own rounding"), FMath::Abs(Double - B) < 2.0e-4);
    TestTrue(TEXT("and the float sees what the double sees, to 1e-6"), FMath::Abs(static_cast<double>(Float) / Double - 1.0) < 1.0e-6);
    return true;
}

bool FAtmosphereEyeBelowTheDatumTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    // Review focus 1: landing's valleys sit under the sphere the law calls
    // the ground. The sky from a valley floor is the sky from the datum.
    const FVector3d Up(0.0, 0.0, 1.0);
    const FAtmosphereScatter Datum = Look(Earth(), FVector3d(0.0, 0.0, 1.0), Up, NoEnd, Up);
    const FAtmosphereScatter Valley = Look(Earth(), FVector3d(0.0, 0.0, 0.999), Up, NoEnd, Up);
    TestTrue(TEXT("the sky from the datum is a sky"), Luminance(Datum.InScatter) > 0.0);
    TestTrue(TEXT("from a valley under the datum it is finite"), IsFinite(Valley.InScatter) && IsFinite(Valley.Transmittance));
    TestTrue(TEXT("and it is the datum's"), (Valley.InScatter - Datum.InScatter).GetAbsMax() <= 1.0e-12 * Datum.InScatter.GetAbsMax());

    const FAtmosphereScatter Down = Look(Earth(), FVector3d(0.0, 0.0, 1.0), -Up, NoEnd, Up);
    TestTrue(TEXT("looking into the ground from it adds nothing and dims nothing"),
        Down.InScatter == FVector3d::ZeroVector && Down.Transmittance == FVector3d::OneVector);

    const FAtmosphereScatter Centre = Look(Earth(), FVector3d::ZeroVector, Up, NoEnd, Up);
    const FAtmosphereScatter Centre32 = Look32(Earth(), FVector3d::ZeroVector, Up, NoEnd, Up);
    TestTrue(TEXT("an eye at the very centre is finite, in double and float"),
        IsFinite(Centre.InScatter) && IsFinite(Centre32.InScatter) && IsFinite(Centre32.Transmittance));
    TestTrue(TEXT("and the float valley is the float datum"),
        IsFinite(Look32(Earth(), FVector3d(0.0, 0.0, 0.999), Up, NoEnd, Up).InScatter));
    return true;
}

bool FAtmosphereNoStarTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    // Review focus 2: FSkyFrame::SunDirection is zero with no star.
    const double H = Earth().GetAir().GasH;
    const FAtmosphereScatter Lit = Limb(Earth(), 2.0 * H, FVector3d(1.0, 0.0, 0.0));
    const FAtmosphereScatter Dark = Limb(Earth(), 2.0 * H, FVector3d::ZeroVector);
    TestTrue(TEXT("no star, no light in the air"), Dark.InScatter == FVector3d::ZeroVector);
    TestTrue(TEXT("but the air still dims what is behind it, as much as by day"), Dark.Transmittance == Lit.Transmittance);
    TestTrue(TEXT("no star, no sunlight through the air"),
        AtmosphereLaw::SunThroughF64(Earth().GetAir(), FVector3d(0.0, 0.0, 1.0), FVector3d::ZeroVector) == FVector3d::ZeroVector);
    const FAtmosphereScatter Dark32 = Look32(Earth(), FVector3d(1.0 + 2.0 * H, -7.8, 0.0), FVector3d(0.0, 1.0, 0.0), NoEnd, FVector3d::ZeroVector);
    TestTrue(TEXT("and the same in float"), Dark32.InScatter == FVector3d::ZeroVector && IsFinite(Dark32.Transmittance));
    return true;
}

bool FAtmosphereDegenerateRaysTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereLawTestLocal;
    // Review focus 3.
    const FVector3d Up(0.0, 0.0, 1.0);
    const FVector3d Far(0.0, 0.0, 7.8);

    const FAtmosphereScatter Through = Look(Earth(), Far, -Up, NoEnd, Up);
    const FAtmosphereScatter ToGround = Look(Earth(), Far, -Up, 6.8, Up);
    TestTrue(TEXT("a view straight at the centre is finite and lit"), IsFinite(Through.InScatter) && Luminance(Through.InScatter) > 0.0);
    TestTrue(TEXT("and ends at the ground, however far it was told to go"),
        Through.InScatter == ToGround.InScatter && Through.Transmittance == ToGround.Transmittance);

    for (const double Length : {0.0, -5.0})
    {
        const FAtmosphereScatter None = Look(Earth(), FVector3d(0.0, 0.0, 1.0), Up, Length, Up);
        TestTrue(FString::Printf(TEXT("a ray of length %.0f adds nothing and dims nothing"), Length),
            None.InScatter == FVector3d::ZeroVector && None.Transmittance == FVector3d::OneVector);
    }

    const FAtmosphereScatter Endless = Look(Earth(), FVector3d(0.0, 0.0, 1.0), Up, NoEnd, Up);
    const FAtmosphereScatter Million = Look(Earth(), FVector3d(0.0, 0.0, 1.0), Up, 1.0e6, Up);
    TestTrue(TEXT("to infinity is to the air's top"), Endless.InScatter == Million.InScatter && Endless.Transmittance == Million.Transmittance);

    const FVector3d Distant(0.0, 1.0e6, 0.0);
    const FVector3d Back = (FVector3d(1.0 + 2.0 * Earth().GetAir().GasH, 0.0, 0.0) - Distant).GetSafeNormal();
    TestTrue(TEXT("a limb from a million radii is finite, in double and float"),
        IsFinite(Look(Earth(), Distant, Back, NoEnd, FVector3d(1.0, 0.0, 0.0)).InScatter)
        && IsFinite(Look32(Earth(), Distant, Back, NoEnd, FVector3d(1.0, 0.0, 0.0)).InScatter));
    return true;
}

#endif
