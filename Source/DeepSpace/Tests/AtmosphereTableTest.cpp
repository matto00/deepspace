#include "Misc/AutomationTest.h"
#include "Atmosphere/Atmosphere.h"
#include "Atmosphere/AtmosphereReference.h"
#include "Tests/AtmosphereTestFixtures.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAtmosphereMultiScatterTableTest, "DeepSpace.Atmosphere.MultiScatterTable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AtmosphereTableTestLocal
{
    double Luminance(const FVector3d& Colour)
    {
        return 0.2126 * Colour.X + 0.7152 * Colour.Y + 0.0722 * Colour.Z;
    }

    /** Straight up from the ground under the noon sun, 45 degrees high. */
    FVector3d NoonZenith(const FAtmosphere& Air)
    {
        const FVector3d Up(0.0, 0.0, 1.0);
        return AtmosphereLaw::InScatterF64(Air.GetAir(), Air.GetTable(), Up, Up, AtmosphereLaw::NoEnd, AtmosphereLaw::NoonSun()).InScatter;
    }
}

bool FAtmosphereMultiScatterTableTest::RunTest(const FString& Parameters)
{
    using namespace AtmosphereTableTestLocal;
    using namespace AtmosphereTestFixtures;
    constexpr int32 Size = FAtmosphereTable::Size;
    constexpr int32 Bins = AtmosphereBins::Count;

    const FAtmosphere Full = FAtmosphere::Build(EarthAir(), SunK);
    const FAtmosphere CarbonDioxideAir = FAtmosphere::Build(CarbonDioxide(CarbonDioxideCeilingBar), SunK);
    const FAirSpec Specs[] = {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar)};
    const FAtmosphere* const Laws[] = {&Full, &CarbonDioxideAir};
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const FAirSpec& Spec = Specs[Index];
        const FAtmosphere& Law = *Laws[Index];
        const FReferenceAir Reference(Spec, SunK);
        const AtmosphereReference::FSpectrum Star = AtmosphereReference::StarSpectrum(SunK);
        const FAtmosphereTable& Table = Law.GetTable();
        if (!TestFalse(TEXT("an airy world has a table"), Table.IsEmpty()))
        {
            return false;
        }
        bool bFinite = true;
        for (const float Texel : Table.Texels)
        {
            bFinite &= std::isfinite(Texel) && Texel >= 0.0f;
        }
        TestTrue(TEXT("every texel finite and not negative"), bFinite);

        // Texel centres: altitude J / 31 of the air's depth, sun cosine
        // -1 + 2 I / 31 -- overhead, 29 degrees up, 5.6 up, 5.6 down -- each
        // bin against the reference's source averaged into it.
        for (const int32 J : {0, 3, 9})
        {
            for (const int32 I : {31, 23, 17, 14})
            {
                const double Altitude01 = static_cast<double>(J) / (Size - 1);
                const double Cos = -1.0 + 2.0 * I / (Size - 1);
                const AtmosphereBins::FBins Want = AtmosphereBins::Average(Star, Reference.MultiScatterSpectrum(Altitude01, Cos));
                for (int32 K = 0; K < Bins; ++K)
                {
                    const double Got = Table.Texel(J, I, K);
                    TestTrue(FString::Printf(TEXT("altitude %.3f, sun cosine %.4f, bin %d: the table's %.5f against the reference's %.5f"),
                        Altitude01, Cos, K, Got, Want.Value[K]),
                        FMath::Abs(Got - Want.Value[K]) <= FMath::Max(0.10 * FMath::Abs(Want.Value[K]), 1.0e-3));
                }
            }
        }

        // Read between texel centres, bilinearly.
        double Read[Bins];
        Table.Sample(0.0, -1.0 + 2.0 * 30.5 / (Size - 1), Read);
        TestTrue(TEXT("halfway between two texels the table reads their mean"),
            FMath::Abs(Read[0] - 0.5 * (Table.Texel(0, 30, 0) + Table.Texel(0, 31, 0))) < 1.0e-6);
    }

    // Coverage. The noon sun's cosine, sin 45 degrees = 0.7071, lies between
    // columns 26 (0.677) and 27 (0.742).
    const FAtmosphere Noon = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::NoonOnly);
    const FAtmosphere NoTable = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    bool bNoonColumns = true;
    bool bRestZero = true;
    for (int32 J = 0; J < Size; ++J)
    {
        for (int32 I = 0; I < Size; ++I)
        {
            for (int32 K = 0; K < Bins; ++K)
            {
                const float Only = Noon.GetTable().Texel(J, I, K);
                if (I == 26 || I == 27)
                {
                    bNoonColumns &= Only == Full.GetTable().Texel(J, I, K);
                }
                else
                {
                    bRestZero &= Only == 0.0f;
                }
            }
        }
    }
    TestTrue(TEXT("the noon table's two columns are the full table's, exactly"), bNoonColumns);
    TestTrue(TEXT("and it has nothing else"), bRestZero);
    TestTrue(TEXT("no table when none is asked for"), NoTable.GetTable().IsEmpty());
    TestTrue(TEXT("the noon zenith reads the same from either table"), NoonZenith(Full) == NoonZenith(Noon));

    // Multiple scattering adds light.
    TestTrue(FString::Printf(TEXT("the table brightens the noon zenith (%.4f with, %.4f without)"),
        Luminance(NoonZenith(Full)), Luminance(NoonZenith(NoTable))),
        Luminance(NoonZenith(Full)) > 1.01 * Luminance(NoonZenith(NoTable)));
    return true;
}

#endif
