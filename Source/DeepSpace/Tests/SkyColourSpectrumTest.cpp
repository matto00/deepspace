#include "Misc/AutomationTest.h"
#include "Sky/SkyColour.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyOneBlackbodyTest,
    "DeepSpace.Sky.OneBlackbody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SkyColourSpectrumTestLocal
{
    /** A light as Blackbody gives one: no channel below zero, the brightest 1. */
    FLinearColor Chromaticity(FLinearColor Colour)
    {
        Colour.R = FMath::Max(Colour.R, 0.0f);
        Colour.G = FMath::Max(Colour.G, 0.0f);
        Colour.B = FMath::Max(Colour.B, 0.0f);
        const float Peak = FMath::Max3(Colour.R, Colour.G, Colour.B);
        if (Peak > 0.0f)
        {
            Colour.R /= Peak;
            Colour.G /= Peak;
            Colour.B /= Peak;
        }
        Colour.A = 1.0f;
        return Colour;
    }
}

bool FSkyOneBlackbodyTest::RunTest(const FString& Parameters)
{
    using namespace SkyColourSpectrumTestLocal;
    const auto Clear = [](double) { return 1.0; };

    // -- The integral on its own -------------------------------------------------
    const FLinearColor Black = SkyColour::ThroughFilter(5772.0, [](double) { return 0.0; });
    TestTrue(TEXT("through an opaque filter a star is black"), Black.R == 0.0f && Black.G == 0.0f && Black.B == 0.0f);

    const FLinearColor Sun = SkyColour::ThroughFilter(5772.0, Clear);
    TestTrue(FString::Printf(TEXT("a Sun through nothing is a light, every channel positive (%.4g, %.4g, %.4g)"), Sun.R, Sun.G, Sun.B),
        Sun.R > 0.0f && Sun.G > 0.0f && Sun.B > 0.0f);

    const FLinearColor Red = SkyColour::ThroughFilter(5772.0, [](double Nm) { return Nm >= 600.0 ? 1.0 : 0.0; });
    TestTrue(TEXT("a filter that passes only 600 nm and up leaves red, and next to no blue"),
        Red.R > 0.0f && Red.B < 0.05f * Red.R);

    const FLinearColor Hot = SkyColour::ThroughFilter(15000.0, Clear);
    const FLinearColor Cool = SkyColour::ThroughFilter(2000.0, Clear);
    TestTrue(TEXT("a hot star is bluer than red, a cool one redder than blue"), Hot.B > Hot.R && Cool.R > Cool.B);

    TestTrue(TEXT("a brightness, not a chromaticity: a 6,000 K surface outshines a 3,000 K one"),
        SkyColour::ThroughFilter(6000.0, Clear).G > 10.0f * SkyColour::ThroughFilter(3000.0, Clear).G);

    const FLinearColor Under = SkyColour::ThroughFilter(500.0, Clear);
    const FLinearColor Floor = SkyColour::ThroughFilter(1000.0, Clear);
    TestTrue(TEXT("clamped as Blackbody clamps: 500 K integrates as 1,000 K"),
        Under.R == Floor.R && Under.G == Floor.G && Under.B == Floor.B);

    // -- Blackbody against it (decision 3, sign-off item 3) ------------------------
    float Worst = 0.0f;
    int32 WorstK = 0;
    for (int32 Kelvin = 2000; Kelvin <= 15000; Kelvin += 250)
    {
        const FLinearColor Fit = SkyColour::Blackbody(Kelvin);
        const FLinearColor Integral = Chromaticity(SkyColour::ThroughFilter(Kelvin, Clear));
        const float Diff = FMath::Max3(FMath::Abs(Fit.R - Integral.R), FMath::Abs(Fit.G - Integral.G), FMath::Abs(Fit.B - Integral.B));
        if (Diff > Worst)
        {
            Worst = Diff;
            WorstK = Kelvin;
        }
    }
    AddInfo(FString::Printf(TEXT("Blackbody against ThroughFilter, both with the brightest channel 1: worst %.4f, at %d K"), Worst, WorstK));
    TestTrue(TEXT("Blackbody is the spectral integral to within 2% of its brightest channel, 2,000-15,000 K"), Worst <= 0.02f);
    return true;
}

#endif
