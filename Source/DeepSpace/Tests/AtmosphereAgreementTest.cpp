#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkySystem.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereLawMatchesReferenceTest, "DeepSpace.Atmosphere.LawMatchesReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// Outside DeepSpace., so the default suite does not run it (Global
// Constraints): minutes of second-order reference traces. Run by name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereFullLawMatchesReferenceTest, "Atmosphere.Full.LawMatchesReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
// Outside DeepSpace. too: the law against the reference's full second
// order, whose phase the law's isotropic table leaves out (ruling 7).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereFullMultipleScatteringGapTest, "Atmosphere.Full.MultipleScatteringGap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereFloatMatchesDoubleTest, "DeepSpace.Atmosphere.FloatMatchesDouble",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereHomothetyInvarianceTest, "DeepSpace.Atmosphere.HomothetyInvariance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereAgreementTestLocal
{
    using AtmosphereTestFixtures::FRayCase;

    /** Decision 1's float extremes on top of the grid: a sun 30 degrees
     *  below the horizon inside the air, and limbs from 7.8 and 1,000 radii
     *  in no special axes -- turned as .ImpactParameter turns its ray, since
     *  an eye on an axis makes every cross product exact and would let
     *  float's cancellation through unseen. */
    TArray<FRayCase> FloatExtremes(double GasH)
    {
        TArray<FRayCase> Cases;
        const double Thirty = FMath::DegreesToRadians(30.0);
        const FVector3d Under(std::cos(Thirty), 0.0, -std::sin(Thirty));
        for (const FVector3d& View : {FVector3d(0.0, 0.0, 1.0), FVector3d(1.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0)})
        {
            FRayCase Case;
            Case.Name = TEXT("inside, a sun 30 degrees below the horizon");
            Case.Eye = FVector3d(0.0, 0.0, 1.0 + 2.0 * GasH);
            Case.Direction = View;
            Case.Sun = Under;
            Cases.Add(Case);
        }
        const FQuat Turn(FVector(0.3, 0.5, 0.8).GetSafeNormal(), 0.7);
        for (const double R : {7.8, 1000.0})
        {
            const double SinAngle = (1.0 + 2.0 * GasH) / R;
            for (const FVector3d& Sun : {FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 0.0, -1.0)})
            {
                FRayCase Case;
                Case.Name = FString::Printf(TEXT("the limb from %.1f radii"), R);
                Case.Eye = Turn.RotateVector(FVector(0.0, 0.0, R));
                Case.Direction = Turn.RotateVector(FVector(SinAngle, 0.0, -std::sqrt(1.0 - SinAngle * SinAngle)));
                Case.Sun = Turn.RotateVector(Sun);
                Cases.Add(Case);
            }
        }
        return Cases;
    }

    bool Within(double Got, double Want, double Relative, double Absolute)
    {
        return std::isfinite(Got) && FMath::Abs(Got - Want) <= FMath::Max(Relative * FMath::Abs(Want), Absolute);
    }

    /** As a display shows it: no channel below none. The reference's
     *  spectral light can fall outside the sRGB gamut -- a deep orange whose
     *  blue is negative -- where non-negative light cannot follow and no
     *  screen could show the difference. */
    double Shown(double Channel)
    {
        return FMath::Max(Channel, 0.0);
    }

    struct FAgreement
    {
        int32 Checked = 0;
        int32 Clamped = 0;   // values Shown changed on either side (planning note 17)
        double Worst = 0.0;
        TArray<FString> Misses;
    };

    /**
     * Both builds of the law against the reference, for each air under each
     * star, over the grid's rays whose name starts with one of Rays ("ground
     * eye, horizon", say; every ray when Rays is empty), each channel
     * within Relative or Absolute. Isotropic: the reference's second
     * scattering sent every way alike, as the law's table sends every order
     * past the first (atmosphere plan ruling 7). Worst is the largest miss
     * over its allowance, met or not.
     */
    FAgreement LawAgainstReference(const TArray<AtmosphereTestFixtures::FNamedAir>& Airs, const TArray<double>& Kelvins, const TArray<FString>& Rays,
                                   bool bIsotropic, double Relative, double Absolute)
    {
        FAgreement Out;
        FReferenceAir::FOptions Options;
        Options.bIsotropicSecondOrder = bIsotropic;
        for (const AtmosphereTestFixtures::FNamedAir& Named : Airs)
        {
            for (const double Kelvin : Kelvins)
            {
                const FAtmosphere Law = FAtmosphere::Build(Named.Air, Kelvin);
                const FReferenceAir Reference(Named.Air, Kelvin);
                for (const FRayCase& Case : AtmosphereTestFixtures::Grid(Law.GetAir().GasH, Law.GetAir().Top))
                {
                    if (!Rays.IsEmpty() && !Rays.ContainsByPredicate([&Case](const FString& Ray) { return Case.Name.StartsWith(Ray); }))
                    {
                        continue;
                    }
                    FReferenceAir::FRay Ray;
                    Ray.Eye = Case.Eye;
                    Ray.Direction = Case.Direction;
                    Ray.Length = Case.Length;
                    Ray.Sun = Case.Sun;
                    const FReferenceAir::FResult Want = Reference.Trace(Ray, Options);
                    const FAtmosphereScatter F64 = AtmosphereLaw::InScatterF64(Law.GetAir(), Law.GetTable(), Case.Eye, Case.Direction, Case.Length, Case.Sun);
                    const FAtmosphereScatter F32 = AtmosphereLaw::InScatterF32(Law.GetAir(), Law.GetTable(),
                        FVector3f(Case.Eye), FVector3f(Case.Direction), static_cast<float>(Case.Length), FVector3f(Case.Sun));
                    for (const FAtmosphereScatter* Got : {&F64, &F32})
                    {
                        for (int32 C = 0; C < 3; ++C)
                        {
                            const double Raw[2][2] = {{Got->InScatter[C], Want.InScatter[C]}, {Got->Transmittance[C], Want.Transmittance[C]}};
                            const double Pairs[2][2] = {{Shown(Got->InScatter[C]), Shown(Want.InScatter[C])},
                                                        {Shown(Got->Transmittance[C]), Shown(Want.Transmittance[C])}};
                            for (int32 Q = 0; Q < 2; ++Q)
                            {
                                ++Out.Checked;
                                Out.Clamped += (Raw[Q][0] < 0.0 || Raw[Q][1] < 0.0) ? 1 : 0;
                                const double Allowance = FMath::Max(Relative * Pairs[Q][1], Absolute);
                                const double Over = std::isfinite(Pairs[Q][0]) ? FMath::Abs(Pairs[Q][0] - Pairs[Q][1]) / Allowance : 1.0e30;
                                Out.Worst = FMath::Max(Out.Worst, Over);
                                if (!(Over <= 1.0))
                                {
                                    Out.Misses.Add(FString::Printf(TEXT("%s, %.0f K, %s, %s %s channel %d: %.5f against %.5f"), Named.Name, Kelvin, *Case.Name,
                                        Got == &F64 ? TEXT("F64") : TEXT("F32"), Q == 0 ? TEXT("in-scatter") : TEXT("transmittance"), C, Pairs[Q][0], Pairs[Q][1]));
                                }
                            }
                        }
                    }
                }
            }
        }
        return Out;
    }

    /** Checked, when Expected is positive, must be exactly Expected: a share
     *  picked by name can shrink when the fixture changes and still pass. */
    void Report(FAutomationTestBase& Test, const FAgreement& Result, const TCHAR* Tolerance, int32 Expected = 0)
    {
        Test.AddInfo(FString::Printf(TEXT("the law against the reference: %d channel values checked, %d outside %s, the worst at %.2f of its allowance; %d changed by the gamut clamp"),
            Result.Checked, Result.Misses.Num(), Tolerance, Result.Worst, Result.Clamped));
        for (int32 I = 0; I < FMath::Min(Result.Misses.Num(), 40); ++I)
        {
            Test.AddInfo(Result.Misses[I]);
        }
        Test.TestTrue(TEXT("the grid checked something: an empty grid is not agreement"), Result.Checked > 0);
        if (Expected > 0)
        {
            Test.TestEqual(TEXT("the share checked every value it was chosen for"), Result.Checked, Expected);
        }
        Test.TestEqual(FString::Printf(TEXT("every channel of both builds within %s of the reference"), Tolerance), Result.Misses.Num(), 0);
    }
}

bool FAtmosphereLawMatchesReferenceTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // The default suite's share of the grid: its hard case, CO2 at its
    // ceiling under the home star, where a red dwarf's small blue must come
    // through a long horizontal path (planning note 13), along the horizon
    // from the ground and from inside the air.
    // Atmosphere.Full.LawMatchesReference is the whole grid.
    const TArray<FNamedAir> Hardest = {{TEXT("CO2 at its ceiling"), CarbonDioxide(CarbonDioxideCeilingBar)}};
    // Six rays (three suns from each of the two eyes), two builds, in-scatter
    // and transmittance, three channels: 72 values, pinned so a renamed eye
    // or a dropped sun cannot quietly shrink the hard case.
    Report(*this, LawAgainstReference(Hardest, {HomeStarK}, {TEXT("ground eye, horizon"), TEXT("inside eye, horizon")}, true, 0.05, 1.0e-3), TEXT("5% or 1e-3"),
        6 * 2 * 2 * 3);
    return true;
}

bool FAtmosphereFullLawMatchesReferenceTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // Decision 1's grid: every mix at 0.05 bar and at its ceiling, under the
    // home star and the Sun, from all four eyes.
    Report(*this, LawAgainstReference(Extremes(), {HomeStarK, SunK}, {}, true, 0.05, 1.0e-3), TEXT("5% or 1e-3"));
    return true;
}

bool FAtmosphereFullMultipleScatteringGapTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // What the law's isotropic multiple scattering costs against the
    // reference's full second order, over the same grid, held to ruling 7's
    // bound so the gap cannot grow unseen. Its worst cases are backlit
    // horizons -- the haze's forward peak, which an isotropic table cannot
    // aim -- and nadir views from inside the ceiling airs.
    Report(*this, LawAgainstReference(Extremes(), {HomeStarK, SunK}, {}, false, 0.25, 1.0e-3), TEXT("25% or 1e-3"));
    return true;
}

bool FAtmosphereFloatMatchesDoubleTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereAgreementTestLocal;
    using namespace AtmosphereTestFixtures;

    // The noon table only: a full one costs over a second a world, and float
    // against double is the same arithmetic whatever the table holds.
    // Decision 1's grid of airs -- every mix at 0.05 bar and at its ceiling
    // -- and the giant.
    TArray<FAirSpec> Airs;
    for (const FNamedAir& Named : Extremes())
    {
        Airs.Add(Named.Air);
    }
    Airs.Add(Giant());
    int32 Checked = 0;
    bool bFinite = true;
    TArray<FString> Misses;
    for (const FAirSpec& Spec : Airs)
    {
        for (const double Kelvin : {HomeStarK, SunK})
        {
            const FAtmosphere Air = FAtmosphere::Build(Spec, Kelvin, EAtmosphereTable::NoonOnly);
            TArray<FRayCase> Cases = Grid(Air.GetAir().GasH, Air.GetAir().Top);
            Cases.Append(FloatExtremes(Air.GetAir().GasH));
            for (const FRayCase& Case : Cases)
            {
                // The float law on the case rounded to float, the double law on
                // the same rounded inputs: the difference is arithmetic only.
                const FVector3f Eye(Case.Eye);
                const FVector3f Direction(Case.Direction);
                const FVector3f Sun(Case.Sun);
                const float Length = static_cast<float>(Case.Length);
                const FAtmosphereScatter F32 = AtmosphereLaw::InScatterF32(Air.GetAir(), Air.GetTable(), Eye, Direction, Length, Sun);
                const FAtmosphereScatter F64 = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), FVector3d(Eye), FVector3d(Direction),
                    static_cast<double>(Length), FVector3d(Sun));
                const FVector3d SunF32 = AtmosphereLaw::SunThroughF32(Air.GetAir(), Eye, Sun);
                const FVector3d SunF64 = AtmosphereLaw::SunThroughF64(Air.GetAir(), FVector3d(Eye), FVector3d(Sun));
                for (int32 C = 0; C < 3; ++C)
                {
                    Checked += 3;
                    bFinite &= std::isfinite(F32.InScatter[C]) && std::isfinite(F32.Transmittance[C]) && std::isfinite(SunF32[C]);
                    if (!Within(F32.InScatter[C], F64.InScatter[C], 1.0e-3, 1.0e-5)
                        || !Within(F32.Transmittance[C], F64.Transmittance[C], 1.0e-3, 1.0e-5)
                        || !Within(SunF32[C], SunF64[C], 1.0e-3, 1.0e-5))
                    {
                        const double B64 = AtmosphereLaw::ImpactParameterF64(FVector3d(Eye), FVector3d(Direction));
                        const float B32 = AtmosphereLaw::ImpactParameterF32(Eye, Direction);
                        Misses.Add(FString::Printf(TEXT("%.0f K, %s, channel %d: in-scatter %.6g against %.6g, transmittance %.6g against %.6g, sun %.6g against %.6g; impact parameter %.9f against %.9f"),
                            Kelvin, *Case.Name, C, F32.InScatter[C], F64.InScatter[C], F32.Transmittance[C], F64.Transmittance[C],
                            SunF32[C], SunF64[C], static_cast<double>(B32), B64));
                    }
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("float against double: %d values checked, %d outside 1e-3 or 1e-5"), Checked, Misses.Num()));
    for (int32 I = 0; I < FMath::Min(Misses.Num(), 40); ++I)
    {
        AddInfo(Misses[I]);
    }
    TestTrue(TEXT("every float output finite"), bFinite);
    TestEqual(TEXT("float within 1e-3 or 1e-5 of double everywhere"), Misses.Num(), 0);
    return true;
}

bool FAtmosphereHomothetyInvarianceTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereTestFixtures;

    // A Sun and an Earth with Earth's air 1 AU apart; the ship 50 km and
    // 50,000 km up, off to one side, where the proxy is drawn at k = 1 and
    // k = 1e-3.
    FSkySystem System;
    FSkyBody& Star = System.Bodies.AddDefaulted_GetRef();
    Star.Id = TEXT("Star");
    Star.Kind = ESkyBodyKind::Star;
    Star.Position = FUniversePosition();
    Star.Radius = 6.957e10;
    Star.Luminosity = 1.0;
    Star.TemperatureK = SunK;
    FSkyBody& Body = System.Bodies.AddDefaulted_GetRef();
    Body.Id = TEXT("World");
    Body.Kind = ESkyBodyKind::Planet;
    Body.Position = FUniversePosition() + FVector(1.495978707e13, 0.0, 0.0);
    Body.Radius = EarthRadiusCm;

    // No table: what is compared is the eye, and the law's arithmetic on it.
    // A full table costs about 1.4 s and adds the same reads to both sides.
    const FAtmosphere Air = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    const FVector3d Up = FVector3d(-1.0, 0.3, 0.2).GetSafeNormal();
    const FVector3d ToStar = (Star.Position - Body.Position).GetSafeNormal();
    const FVector3d Side = FVector3d::CrossProduct(Up, FVector3d(0.0, 0.0, 1.0)).GetSafeNormal();
    const double Top = Air.GetAir().Top;

    for (const double Altitude : {5.0e6, 5.0e9})
    {
        const FUniversePosition Ship = Body.Position + Up * (Body.Radius + Altitude);
        const FSkyFrame Frame = SkyProjection::Project(System, Ship, FSkyViewParams());
        const FSkyBodyView& View = Frame.Bodies[1];
        const double K = View.ProxyRadius / Body.Radius;
        const FVector3d ProxyEye = -View.ProxyLocation / View.ProxyRadius;
        const FVector3d TrueEye = (Ship - Body.Position) / Body.Radius;
        AddInfo(FString::Printf(TEXT("%.0f km up: the proxy is drawn at k = %.4e; eyes differ by %.3e radii"),
            Altitude / 1.0e5, K, (ProxyEye - TrueEye).Size()));
        TestTrue(TEXT("the proxy's scale is the altitude's: about 1 at 50 km, about 1e-3 at 50,000 km"),
            K > 0.5 * 5.0e6 / Altitude && K < 2.0 * 5.0e6 / Altitude);
        // Ruling 5: two roundings of one ratio, so 1e-12 on the eyes and
        // 1e-9 on what the law makes of them, not bit for bit.
        TestTrue(TEXT("the eye in the proxy's radii is the true eye in the world's, to 1e-12"),
            (ProxyEye - TrueEye).Size() <= 1.0e-12 * TrueEye.Size());
        // The views are set by impact parameter, so each one crosses the air
        // from either altitude: nadir; the ground half a radius off the axis,
        // obliquely; and the limb, grazing 0.3 of the way up the air. From
        // 50,000 km the air is under 7 degrees across, and a fixed angle off
        // nadir misses it; a radial view there sees the same air wherever the
        // eye sits along it, so only the oblique two can tell a radially
        // misplaced eye -- which is what a wrong homothety makes.
        const double Distance = TrueEye.Size();
        const TPair<const TCHAR*, double> Aims[] = {{TEXT("nadir"), 0.0}, {TEXT("the ground, obliquely"), 0.5}, {TEXT("the limb"), 1.0 + 0.3 * Top}};
        for (const TPair<const TCHAR*, double>& Aim : Aims)
        {
            const double Sin = Aim.Value / Distance;
            const FVector3d Direction = -Up * FMath::Sqrt(1.0 - Sin * Sin) + Side * Sin;
            const FAtmosphereScatter FromProxy = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), ProxyEye, Direction, AtmosphereLaw::NoEnd, ToStar);
            const FAtmosphereScatter FromTrue = AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), TrueEye, Direction, AtmosphereLaw::NoEnd, ToStar);
            TestTrue(FString::Printf(TEXT("%.0f km up, %s: the view crosses the air, so the check below can fail"), Altitude / 1.0e5, Aim.Key),
                FromTrue.Transmittance[2] < 1.0 - 1.0e-6 && FromTrue.InScatter[2] > 0.0);
            for (int32 C = 0; C < 3; ++C)
            {
                TestTrue(FString::Printf(TEXT("%.0f km up, %s, channel %d: the air term does not see the proxy's scale"), Altitude / 1.0e5, Aim.Key, C),
                    FMath::Abs(FromProxy.InScatter[C] - FromTrue.InScatter[C]) <= 1.0e-9 * FMath::Max(FMath::Abs(FromTrue.InScatter[C]), 1.0e-12)
                    && FMath::Abs(FromProxy.Transmittance[C] - FromTrue.Transmittance[C]) <= 1.0e-9);
            }
        }
    }
    return true;
}

#endif
