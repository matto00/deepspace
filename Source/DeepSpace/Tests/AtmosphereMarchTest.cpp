#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereSingleScatteringTest, "DeepSpace.Atmosphere.SingleScatteringMatchesReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereMarchTestLocal
{
    /** As a display shows it: no channel below none (the reference's light
     *  can leave sRGB's gamut, where no screen could show the difference).
     *  No ruling approved this, so the test counts what it changes
     *  (planning note 17). */
    double Shown(double Channel)
    {
        return FMath::Max(Channel, 0.0);
    }
}

bool FAtmosphereSingleScatteringTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereMarchTestLocal;
    using namespace AtmosphereTestFixtures;

    // The law's march and its bins, with multiple scattering out of both:
    // the law with no table against the reference's first order, over
    // decision 1's whole grid, both builds, at decision 1's 5% or 1e-3.
    // The cases that fail a midpoint rule are here -- the haze's 1 km under
    // H2/He's 55 km, the horizon through a ceiling air, a limb whose light
    // climbs out of the ground's shadow -- and so are the long paths where
    // one depth per bin must stand for several.
    FReferenceAir::FOptions FirstOrder;
    FirstOrder.bSecondOrder = false;
    int32 Checked = 0;
    int32 Clamped = 0;
    double Worst = 0.0;
    TArray<FString> Misses;
    for (const FNamedAir& Named : Extremes())
    {
        // The home star and the Sun, which the bins were chosen against, and
        // procgen's coolest star, which they were not (planning note 13).
        for (const double Kelvin : {HomeStarK, CoolestStarK, SunK})
        {
            const FAtmosphere Law = FAtmosphere::Build(Named.Air, Kelvin, EAtmosphereTable::None);
            const FReferenceAir Reference(Named.Air, Kelvin);
            for (const FRayCase& Case : Grid(Law.GetAir().GasH, Law.GetAir().Top))
            {
                FReferenceAir::FRay Ray;
                Ray.Eye = Case.Eye;
                Ray.Direction = Case.Direction;
                Ray.Length = Case.Length;
                Ray.Sun = Case.Sun;
                const FReferenceAir::FResult Want = Reference.Trace(Ray, FirstOrder);
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
                            ++Checked;
                            Clamped += (Raw[Q][0] < 0.0 || Raw[Q][1] < 0.0) ? 1 : 0;
                            const double Allowance = FMath::Max(0.05 * Pairs[Q][1], 1.0e-3);
                            const double Over = std::isfinite(Pairs[Q][0]) ? FMath::Abs(Pairs[Q][0] - Pairs[Q][1]) / Allowance : 1.0e30;
                            Worst = FMath::Max(Worst, Over);
                            if (!(Over <= 1.0))
                            {
                                Misses.Add(FString::Printf(TEXT("%s, %.0f K, %s, %s %s channel %d: %.5f against %.5f"), Named.Name, Kelvin, *Case.Name,
                                    Got == &F64 ? TEXT("F64") : TEXT("F32"), Q == 0 ? TEXT("in-scatter") : TEXT("transmittance"), C, Pairs[Q][0], Pairs[Q][1]));
                            }
                        }
                    }
                }
            }
        }
    }
    AddInfo(FString::Printf(TEXT("single scattering against the reference's first order: %d values, %d outside 5%% or 1e-3, the worst at %.2f of its allowance; %d changed by the gamut clamp"),
        Checked, Misses.Num(), Worst, Clamped));
    for (int32 I = 0; I < FMath::Min(Misses.Num(), 40); ++I)
    {
        AddInfo(Misses[I]);
    }
    TestEqual(TEXT("the whole grid was checked: 18 airs and stars, 48 rays, two builds, three channels, two quantities"), Checked, 18 * 48 * 2 * 3 * 2);
    TestEqual(TEXT("every channel of both builds within 5% or 1e-3 of the reference's first order"), Misses.Num(), 0);
    return true;
}

#endif
