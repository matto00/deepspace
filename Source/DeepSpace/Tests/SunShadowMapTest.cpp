#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/SunShadowMap.h"
#include "Surface/WorldRelief.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapShapeTest, "DeepSpace.Surface.SunShadowMap.Shape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapTexelsTest, "DeepSpace.Surface.SunShadowMap.TexelsAreTheProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapNightTest, "DeepSpace.Surface.SunShadowMap.NightBelowTheMap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapLookupTest, "DeepSpace.Surface.SunShadowMap.Lookup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowMapMipsTest, "DeepSpace.Surface.SunShadowMap.Mips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SunShadowMapTestLocal
{
    /** Baemsekai IV-like ground under a star 1.66 degrees in radius, from an
     *  oblique direction. */
    SunShadow::FSunLight Light()
    {
        SunShadow::FSunLight Sun;
        Sun.Direction = FVector3d(0.3, 0.4, FMath::Sqrt(0.75)).GetSafeNormal();
        Sun.AngularRadius = 0.02903;
        return Sun;
    }

    /** A hand-made map, 16 columns by 4 rows, every row's centre under pi/2,
     *  its frame the body's axes; each texel 1000 + 3000 x column + 700 x row. */
    FSunShadowMap Hand()
    {
        FSunShadowMap Map;
        Map.Width = 16;
        Map.Rows = 4;
        Map.PsiLo = -0.1;
        Map.Step = 2.0 * UE_DOUBLE_PI / 16.0;
        Map.Levels.SetNum(1);
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            for (int32 Column = 0; Column < Map.Width; ++Column)
            {
                Map.Levels[0].Add(static_cast<uint16>(1000 + 3000 * Column + 700 * Row));
            }
        }
        SunShadowMap::BuildLevels(Map);
        return Map;
    }

    /** The direction at azimuth Phi and elevation Psi in the map's frame. */
    FVector3d At(const FSunShadowMap& Map, double Phi, double Psi)
    {
        return (Map.FrameX * FMath::Cos(Phi) + Map.FrameY * FMath::Sin(Phi)) * FMath::Cos(Psi) + Map.FrameZ * FMath::Sin(Psi);
    }
}

bool FSunShadowMapShapeTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    const FReliefGround Ground(GroundFixtures::FixtureParams());
    const SunShadow::FSunLight Sun = Light();
    const FSunShadowMap Map = SunShadowMap::Shape(Ground, Sun, 64);
    const double R = Ground.RadiusCm();
    TestEqual(TEXT("64 columns"), Map.Width, 64);
    TestTrue(TEXT("each 2 pi / 64 of azimuth"), FMath::IsNearlyEqual(Map.Step, 2.0 * UE_DOUBLE_PI / 64.0, 1e-15));
    const double PsiLo = -(SunShadow::NightDip(R, Ground.MaxHeightCm(), Ground.MinHeightCm()) + Sun.AngularRadius);
    TestTrue(FString::Printf(TEXT("the lowest row is where even the peak's view of the lowest ground clears the star's top (%.6f)"), Map.PsiLo),
        FMath::IsNearlyEqual(Map.PsiLo, PsiLo, 1e-15));
    TestEqual(TEXT("and the rows reach the point under the star"), Map.Rows, FMath::CeilToInt32((0.5 * UE_DOUBLE_PI - PsiLo) / Map.Step));
    TestTrue(TEXT("Z is the light"), Map.FrameZ.Equals(Sun.Direction.GetSafeNormal(), 1e-15));
    TestTrue(TEXT("the frame is orthonormal and right-handed"),
        FMath::IsNearlyEqual(Map.FrameX.Size(), 1.0, 1e-15) && FMath::Abs(FVector3d::DotProduct(Map.FrameX, Map.FrameZ)) <= 1e-15
        && FVector3d::CrossProduct(Map.FrameX, Map.FrameY).Equals(Map.FrameZ, 1e-15));
    for (int32 Row = 0; Row < Map.Rows; ++Row)
    {
        const double Psi = Map.PsiLo + (Row + 0.5) * Map.Step;
        if (Psi > 0.5 * UE_DOUBLE_PI)
        {
            continue;   // the last row's centre may lie just past the pole, where no lookup lands on it
        }
        for (int32 Column = 0; Column < Map.Width; ++Column)
        {
            const FVector3d D = SunShadowMap::TexelDirection(Map, Column, Row);
            const double Phi = FMath::Atan2(FVector3d::DotProduct(D, Map.FrameY), FVector3d::DotProduct(D, Map.FrameX));
            TestTrue(TEXT("a texel's elevation is its row's centre"), FMath::IsNearlyEqual(FMath::Asin(FVector3d::DotProduct(D, Map.FrameZ)), Psi, 1e-12));
            TestTrue(TEXT("and its azimuth its column's"), FMath::IsNearlyEqual(Phi, -UE_DOUBLE_PI + (Column + 0.5) * Map.Step, 1e-12));
        }
    }
    TestEqual(TEXT("no star, no map"), SunShadowMap::Shape(Ground, SunShadow::FSunLight(), 64).Width, 0);
    return true;
}

bool FSunShadowMapTexelsTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    // Every texel is AlongProfile over its column's heights ahead of it, read
    // once each at the step, quantised: worked out again here, column by
    // column, and held equal to the bit.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Ground(Params);
    const SunShadow::FSunLight Sun = Light();
    const double Steep = SunShadow::SteepestSlope(Params);
    // 512 columns: a texel of 1.2e-2 rad, a quarter of the march's end
    // near the terminator, so a profile spans several texels ahead. At 128
    // the end was under one texel, and a bake that cut every profile short
    // matched the full one texel for texel.
    const FSunShadowMap Map = SunShadowMap::Bake(Ground, Sun, Steep, 512);
    if (!TestEqual(TEXT("level 0 holds every texel"), Map.LevelCount() > 0 ? Map.Levels[0].Num() : 0, Map.Width * Map.Rows))
    {
        return false;
    }
    const double R = Ground.RadiusCm();
    int32 Wrong = 0;
    int32 Shaded = 0;
    for (int32 Column = 0; Column < Map.Width; Column += 7)
    {
        TArray<double> Heights;
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            Heights.Add(Ground.Height(SunShadowMap::TexelDirection(Map, Column, Row), Map.Step * R));
        }
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            const double Psi = Map.PsiLo + (Row + 0.5) * Map.Step;
            const uint16 Want = SunShadowMap::Quantise(SunShadow::AlongProfile(R, Ground.MaxHeightCm(), Ground.MinHeightCm(), Steep,
                MakeArrayView(Heights.GetData() + Row, Map.Rows - Row), Map.Step, Psi, Sun.AngularRadius).Visible);
            Wrong += Map.Texel(0, Column, Row) == Want ? 0 : 1;
            Shaded += Want < 32768 ? 1 : 0;
        }
    }
    TestEqual(TEXT("every texel is its column's profile, quantised"), Wrong, 0);
    AddInfo(FString::Printf(TEXT("512 columns x %d rows; of the columns checked, %d texels under half"), Map.Rows, Shaded));
    TestEqual(TEXT("a whole texel is 65535"), static_cast<int32>(SunShadowMap::Quantise(1.0)), 65535);
    TestEqual(TEXT("a dark one 0"), static_cast<int32>(SunShadowMap::Quantise(0.0)), 0);
    std::atomic<bool> Stop(true);
    TestEqual(TEXT("a cancelled bake stops and returns nothing"), SunShadowMap::Bake(Ground, Sun, Steep, 128, &Stop).LevelCount(), 0);
    return true;
}

bool FSunShadowMapNightTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    // The rows the map leaves out are dark for every point of the world,
    // whatever its height: the march itself says so at every footprint.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Ground(Params);
    const SunShadow::FSunLight Sun = Light();
    const FSunShadowMap Shape = SunShadowMap::Shape(Ground, Sun, 256);
    const double Steep = SunShadow::SteepestSlope(Params);
    FRandomStream Stream(20260930);
    int32 Lit = 0;
    for (int32 Sample = 0; Sample < 300; ++Sample)
    {
        const FVector3d D(Stream.GetUnitVector());
        const FVector3d Toward(Stream.GetUnitVector());
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        const double Psi = Shape.PsiLo - 1.0e-6 - 0.02 * Stream.GetFraction();
        SunShadow::FSunLight Under = Sun;
        Under.Direction = D * FMath::Sin(Psi) + Level * FMath::Cos(Psi);
        for (const double Footprint : { 0.0, 1.0e4, Shape.Step * Ground.RadiusCm() })
        {
            Lit += SunShadow::Visible(Ground, D, Under, Footprint, Steep).Visible > 0.0 ? 1 : 0;
        }
    }
    TestEqual(TEXT("under the map's lowest row no point sees any of its star"), Lit, 0);
    return true;
}

bool FSunShadowMapLookupTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowMapTestLocal;
    const FSunShadowMap Map = Hand();
    const auto Value = [&](int32 Level, int32 X, int32 Y) { return Map.Texel(Level, X, Y) / 65535.0; };
    // At every texel's centre, with a footprint finer than a texel, the lookup is the texel.
    for (int32 Row = 0; Row < Map.Rows; ++Row)
    {
        for (int32 Column = 0; Column < Map.Width; ++Column)
        {
            const FVector3d D = SunShadowMap::TexelDirection(Map, Column, Row);
            TestTrue(FString::Printf(TEXT("the texel at %d, %d, in double"), Column, Row),
                FMath::IsNearlyEqual(SunShadowMap::Sample(Map, D, 1.0e-9), Value(0, Column, Row), 1e-9));
            TestTrue(FString::Printf(TEXT("and in float, %d, %d"), Column, Row),
                FMath::IsNearlyEqual(static_cast<double>(SunShadowMap::SampleF32(Map, FVector3f(D), 1.0e-9f)), Value(0, Column, Row), 2e-6));
        }
    }
    // The seam: azimuth +-pi lies between the last column and the first, from either side.
    const double RowOne = Map.PsiLo + 1.5 * Map.Step;
    const double Before = SunShadowMap::Sample(Map, At(Map, UE_DOUBLE_PI - 1.0e-7, RowOne), 1.0e-9);
    const double After = SunShadowMap::Sample(Map, At(Map, -UE_DOUBLE_PI + 1.0e-7, RowOne), 1.0e-9);
    const double Between = 0.5 * (Value(0, 15, 1) + Value(0, 0, 1));
    TestTrue(FString::Printf(TEXT("just short of +pi the lookup is the last and first columns' mean (%.8f, %.8f)"), Before, Between),
        FMath::IsNearlyEqual(Before, Between, 1e-5));
    TestTrue(FString::Printf(TEXT("and just past -pi it is the same (%.8f)"), After), FMath::IsNearlyEqual(After, Between, 1e-5));
    // Under PsiLo no point of the world sees any of the star (the night
    // exit's proof), so the lookup is 0 there -- never row 0's value, which a
    // relief normal tilted toward the sun past the terminator would otherwise
    // be lit by. Between PsiLo and row 0's centre the rows clamp to row 0;
    // over the highest, the highest.
    const double Column3 = -UE_DOUBLE_PI + 3.5 * Map.Step;
    TestTrue(TEXT("under PsiLo the lookup is 0, in double"), SunShadowMap::Sample(Map, At(Map, Column3, -0.3), 1.0e-9) == 0.0);
    TestTrue(TEXT("and in float"), SunShadowMap::SampleF32(Map, FVector3f(At(Map, Column3, -0.3)), 1.0e-9f) == 0.0f);
    TestTrue(TEXT("and just under it, at every level"), SunShadowMap::Sample(Map, At(Map, Column3, Map.PsiLo - 1.0e-6), 0.5) == 0.0);
    TestTrue(TEXT("just over PsiLo the lowest row"),
        FMath::IsNearlyEqual(SunShadowMap::Sample(Map, At(Map, Column3, Map.PsiLo + 0.25 * Map.Step), 1.0e-9), Value(0, 3, 0), 1e-9));
    TestTrue(TEXT("over the highest the highest"), FMath::IsNearlyEqual(SunShadowMap::Sample(Map, At(Map, Column3, 1.45), 1.0e-9), Value(0, 3, 3), 1e-9));
    // The level: a footprint twice a texel at the terminator reads level 1.
    const double RowZero = Map.PsiLo + 0.5 * Map.Step;
    const FVector3d Coarse = SunShadowMap::TexelDirection(Map, 2, 0);
    const WorldReliefNoise::FShadowCoord Two = WorldReliefNoise::ShadowMapCoordF64(Coarse, Map.FrameX, Map.FrameZ, Map.PsiLo, Map.Step,
        2.0 * Map.Step * FMath::Cos(RowZero), Map.LevelCount());
    TestTrue(FString::Printf(TEXT("a footprint of two texels is level 1 (%d, blend %.9f)"), Two.Level0, Two.Blend),
        (Two.Level0 == 1 && Two.Blend <= 1e-9) || (Two.Level0 == 0 && Two.Blend >= 1.0 - 1e-9));
    // At level 1 texel (0, 0) covers level 0's columns 0-1, so column 2's centre is 0.75 of the way from it to (1, 0).
    const double LevelOne = WorldReliefNoise::LerpF64(Value(1, 0, 0), Value(1, 1, 0), 0.75);
    TestTrue(FString::Printf(TEXT("and reads level 1's texels there (%.8f against %.8f)"), SunShadowMap::Sample(Map, Coarse, 2.0 * Map.Step * FMath::Cos(RowZero)), LevelOne),
        FMath::IsNearlyEqual(SunShadowMap::Sample(Map, Coarse, 2.0 * Map.Step * FMath::Cos(RowZero)), LevelOne, 1e-6));
    // Toward the point under the star a texel narrows as cos(psi), so the same footprint reads coarser.
    const WorldReliefNoise::FShadowCoord High = WorldReliefNoise::ShadowMapCoordF64(At(Map, Column3, 1.27), Map.FrameX, Map.FrameZ,
        Map.PsiLo, Map.Step, Map.Step, Map.LevelCount());
    TestTrue(FString::Printf(TEXT("one texel's footprint at 1.27 rad up is level log2(1 / cos) = 1.76 (%d + %.3f)"), High.Level0, High.Blend),
        High.Level0 == 1 && FMath::IsNearlyEqual(High.Blend, FMath::Log2(1.0 / FMath::Cos(1.27)) - 1.0, 1e-9));
    // Float against double, anywhere.
    FRandomStream Stream(20261001);
    double Widest = 0.0;
    for (int32 Sample = 0; Sample < 400; ++Sample)
    {
        const FVector3d D(Stream.GetUnitVector());
        const double Footprint = FMath::Pow(10.0, Stream.FRandRange(-4.0, -0.3));
        Widest = FMath::Max(Widest, FMath::Abs(SunShadowMap::Sample(Map, D, Footprint) - SunShadowMap::SampleF32(Map, FVector3f(D), static_cast<float>(Footprint))));
    }
    TestTrue(FString::Printf(TEXT("the float lookup is the double's to 1e-5 (%.2e)"), Widest), Widest <= 1.0e-5);
    TestTrue(TEXT("an empty map is whole"), SunShadowMap::Sample(FSunShadowMap(), FVector3d::UnitZ(), 1.0e-3) == 1.0);
    return true;
}

bool FSunShadowMapMipsTest::RunTest(const FString& Parameters)
{
    // Four by three: level 1 is two by one, each the mean of its two by two
    // (rounded); level 2 is one by one, from level 1's two, its row repeated.
    FSunShadowMap Map;
    Map.Width = 4;
    Map.Rows = 3;
    Map.Levels.SetNum(1);
    Map.Levels[0] = { 0, 6, 8, 12, 100, 104, 108, 112, 60000, 60000, 60000, 60000 };
    SunShadowMap::BuildLevels(Map);
    TestEqual(TEXT("three levels, as the GPU counts a 4 x 3 texture's mips"), Map.LevelCount(), 3);
    TestEqual(TEXT("level 1 is 2 x 1"), Map.WidthAt(1) * 10 + Map.RowsAt(1), 21);
    TestTrue(TEXT("its texels are the rounded means of rows 0 and 1 (row 2 falls away, as the GPU's floor does)"),
        Map.Levels.Num() == 3 && Map.Levels[1] == TArray<uint16>({ 53, 60 }));
    TestTrue(TEXT("level 2 is their mean"), Map.Levels.Num() == 3 && Map.Levels[2] == TArray<uint16>({ 57 }));
    TestEqual(TEXT("and the bytes are every level's"), Map.Bytes(), static_cast<int64>((12 + 2 + 1) * sizeof(uint16)));
    return true;
}

#endif
