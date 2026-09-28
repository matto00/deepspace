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

    for (const FAirSpec& Spec : {EarthAir(), CarbonDioxide(CarbonDioxideCeilingBar)})
    {
        const FAtmosphere Law = FAtmosphere::Build(Spec, SunK);
        const FReferenceAir Reference(Spec, SunK);
        const FAtmosphereTable& Table = Law.GetTable();
        if (!TestFalse(TEXT("an airy world has a table"), Table.IsEmpty()))
        {
            return false;
        }
        bool bFinite = true;
        for (const FVector3f& Texel : Table.Texels)
        {
            bFinite &= std::isfinite(Texel.X) && std::isfinite(Texel.Y) && std::isfinite(Texel.Z) && Texel.GetMin() >= 0.0f;
        }
        TestTrue(TEXT("every texel finite and not negative"), bFinite);

        // Texel centres: altitude J / 31 of the air's depth, sun cosine -1 + 2 I / 31.
        for (const int32 J : {0, 3, 9})
        {
            for (const int32 I : {31, 23, 17, 14})
            {
                const double Altitude01 = static_cast<double>(J) / (Size - 1);
                const double Cos = -1.0 + 2.0 * I / (Size - 1);
                const FVector3f& Texel = Table.Texels[J * Size + I];
                const FVector3d Want = Reference.MultiScatterWhite(Altitude01, Cos);
                for (int32 C = 0; C < 3; ++C)
                {
                    const double Got = Texel[C];
                    TestTrue(FString::Printf(TEXT("altitude %.3f, sun cosine %.3f, channel %d: the table's %.5f against the reference's %.5f"),
                        Altitude01, Cos, C, Got, Want[C]),
                        FMath::Abs(Got - Want[C]) <= FMath::Max(0.10 * FMath::Abs(Want[C]), 1.0e-3));
                }
            }
        }

        // Read between texel centres, bilinearly.
        double R = 0.0;
        double G = 0.0;
        double B = 0.0;
        Table.Sample(0.0, -1.0 + 2.0 * 30.5 / (Size - 1), R, G, B);
        TestTrue(TEXT("halfway between two texels the table reads their mean"),
            FMath::Abs(R - 0.5 * (Table.Texels[30].X + Table.Texels[31].X)) < 1.0e-6);
    }

    // Coverage. The noon sun's cosine, sin 45 degrees = 0.7071, lies between
    // columns 26 (0.677) and 27 (0.742).
    const FAtmosphere Full = FAtmosphere::Build(EarthAir(), SunK);
    const FAtmosphere Noon = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::NoonOnly);
    const FAtmosphere NoTable = FAtmosphere::Build(EarthAir(), SunK, EAtmosphereTable::None);
    bool bColumns = true;
    bool bRestZero = true;
    for (int32 J = 0; J < Size; ++J)
    {
        for (int32 I = 0; I < Size; ++I)
        {
            const FVector3f& Only = Noon.GetTable().Texels[J * Size + I];
            if (I == 26 || I == 27)
            {
                bColumns &= Only == Full.GetTable().Texels[J * Size + I];
            }
            else
            {
                bRestZero &= Only.IsZero();
            }
        }
    }
    TestTrue(TEXT("the noon table's two columns are the full table's, exactly"), bColumns);
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
