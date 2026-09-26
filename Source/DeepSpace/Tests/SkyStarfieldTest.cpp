#include "Misc/AutomationTest.h"
#include "Sky/SkyStarfield.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyStarfieldTest,
    "DeepSpace.Sky.Starfield",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyStarfieldTest::RunTest(const FString& Parameters)
{
    constexpr int32 Count = 3000;
    const TArray<FSkyStar> Stars = SkyStarfield::Generate(20260925, Count);
    TestEqual(TEXT("as many stars as asked for"), Stars.Num(), Count);

    // Same seed, same sky; another seed, another sky.
    {
        const TArray<FSkyStar> Again = SkyStarfield::Generate(20260925, Count);
        const TArray<FSkyStar> Other = SkyStarfield::Generate(20260926, Count);
        bool bSame = true;
        int32 Differ = 0;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            bSame &= Stars[Index].Direction == Again[Index].Direction
                && Stars[Index].Flux == Again[Index].Flux
                && Stars[Index].TemperatureK == Again[Index].TemperatureK;
            Differ += Stars[Index].Direction != Other[Index].Direction ? 1 : 0;
        }
        TestTrue(TEXT("the same seed gives the same stars"), bSame);
        TestEqual(TEXT("another seed moves every star"), Differ, Count);

        // Star i is Seed and i alone: asking for more appends to the sky.
        const TArray<FSkyStar> Fewer = SkyStarfield::Generate(20260925, 100);
        bool bPrefix = true;
        for (int32 Index = 0; Index < Fewer.Num(); ++Index)
        {
            bPrefix &= Fewer[Index].Direction == Stars[Index].Direction && Fewer[Index].Flux == Stars[Index].Flux;
        }
        TestTrue(TEXT("a smaller sky is the start of a larger one"), bPrefix);
        TestEqual(TEXT("no stars is no stars"), SkyStarfield::Generate(1, 0).Num(), 0);
    }

    int32 NearPlane = 0;
    int32 Bright = 0;
    int32 TowardCentre = 0;
    bool bUnit = true;
    bool bFluxInRange = true;
    bool bTemperatureInRange = true;
    TArray<double> Fluxes;
    for (const FSkyStar& Star : Stars)
    {
        bUnit &= FMath::IsNearlyEqual(Star.Direction.Size(), 1.0, 1.0e-9);
        const double Latitude = FMath::Asin(FMath::Clamp(Star.Direction.Z, -1.0, 1.0));
        NearPlane += FMath::Abs(Latitude) < FMath::DegreesToRadians(15.0) ? 1 : 0;
        TowardCentre += Star.Direction.X > 0.0 ? 1 : 0;
        Bright += Star.Flux > 4.0 ? 1 : 0;
        bFluxInRange &= Star.Flux >= 1.0 && Star.Flux <= SkyStarfield::MaxFlux;
        bTemperatureInRange &= Star.TemperatureK >= SkyStarfield::MinTemperatureK && Star.TemperatureK <= SkyStarfield::MaxTemperatureK;
        Fluxes.Add(Star.Flux);
    }
    Fluxes.Sort();
    const double Median = Fluxes[Count / 2];
    const double NearPlaneShare = static_cast<double>(NearPlane) / Count;
    const double BrightShare = static_cast<double>(Bright) / Count;
    AddInfo(FString::Printf(TEXT("within 15 deg of the plane %.1f%%, toward the centre %.1f%%, median flux %.3f, above flux 4 %.1f%%"),
        100.0 * NearPlaneShare, 100.0 * TowardCentre / Count, Median, 100.0 * BrightShare));

    TestTrue(TEXT("every direction is a unit vector"), bUnit);

    // A band: an isotropic sky puts 26% of stars this near the plane.
    TestTrue(TEXT("more than 45% of stars lie within 15 degrees of the plane"), NearPlaneShare > 0.45);

    // One side of the band is richer, toward the galactic centre.
    TestTrue(TEXT("more stars toward the galactic centre than away"), TowardCentre > Count / 2 + Count / 20);

    TestTrue(TEXT("every flux lies in [1, 400]"), bFluxInRange);
    TestTrue(TEXT("every temperature lies in [2800, 15000]"), bTemperatureInRange);

    // Pareto 1.5 puts its median at 2^(2/3), about 1.6; a uniform flux on
    // 1-400 would put it near 200. This is the test a revert to uniform fails.
    TestTrue(TEXT("the median flux is below 2"), Median < 2.0);

    // 4^-1.5 = 12.5% brighter than flux 4: the veil's target (sky decision 6).
    TestTrue(TEXT("about 12% of stars are brighter than flux 4"), FMath::Abs(BrightShare - 0.125) < 0.03);
    return true;
}

#endif
