#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "Sky/ShipSky.h"
#include "Sky/SkySystem.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/StarSystemGenerator.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ShaderCore.h"
#include "Sky/SkyMaterialContract.h"
#include "Surface/WorldRelief.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWorldReliefKnownValuesTest,
    "DeepSpace.Surface.WorldRelief.KnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FShaderMappingTest,
    "DeepSpace.Surface.ShaderMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefDeterministicTest, "DeepSpace.Surface.WorldRelief.Deterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefSeedMatchesSkyTest, "DeepSpace.Surface.WorldRelief.SeedMatchesSky",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefBoundsTest, "DeepSpace.Surface.WorldRelief.Bounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefGradientTest, "DeepSpace.Surface.WorldRelief.Gradient",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefBandLimitTest, "DeepSpace.Surface.WorldRelief.BandLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefFootprintTest, "DeepSpace.Surface.WorldRelief.Footprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefSlopeBoundTest, "DeepSpace.Surface.WorldRelief.SlopeBound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefMeasuredMaxTest, "DeepSpace.Surface.WorldRelief.MeasuredMax",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldReliefPeakCapTest, "DeepSpace.Surface.WorldRelief.PeakCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace WorldReliefTestLocal
{
    /** Worked from the HLSL formulas in double, outside the engine and outside
     *  this code: Rand3DPCG16's LCG step and six Feistel rounds by hand, the
     *  noise from Random.ush's arithmetic. Never read back from a run. */
    constexpr double Tolerance = 1.0e-6;
    const FVector3d A(0.3, 1.7, -2.2);
    const FVector3d B(12.34, -5.67, 89.01);
    /** Inside crater band 104's bowl, 0.11 cells from any step, so a
     *  rounding cannot move it across one. */
    const FVector3d Known(0.6005198731401901, -0.47976589864946456, 0.6396878648659529);
    const FVector3d Offset(12.5, 200.25, 77.0);

    bool Near(double X, double Y) { return FMath::Abs(X - Y) <= Tolerance; }
    bool Near(const FVector3d& X, const FVector3d& Y) { return (X - Y).GetAbsMax() <= Tolerance; }

    /** An Earth-sized world with a 6 km peak, at the known values' offset. */
    FWorldReliefParams Earthlike()
    {
        FWorldReliefParams Params;
        Params.SeedOffset = Offset;
        Params.RadiusCm = 6.3781e8;
        Params.PeakCm = 6.0e5;
        Params.Cratering = 1.0;
        Params.Ground = EGround::Solid;
        return Params;
    }

    /** Directions over the sphere from a fixed stream: a test's sampling, not
     *  a world's draw, so uniform is the right distribution for once. */
    TArray<FVector3d> Directions(int32 Count, int32 Seed)
    {
        FRandomStream Stream(Seed);
        TArray<FVector3d> Out;
        Out.Reserve(Count);
        for (int32 Sample = 0; Sample < Count; ++Sample)
        {
            Out.Add(FVector3d(Stream.GetUnitVector()));
        }
        return Out;
    }

    /** A unit vector square to D. */
    FVector3d Tangent(const FVector3d& D)
    {
        return FVector3d::CrossProduct(D, FMath::Abs(D.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX()).GetSafeNormal();
    }
}

bool FWorldReliefKnownValuesTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;

    // -- The hash, bit for bit ------------------------------------------------
    struct FHashCase { int32 X; int32 Y; int32 Z; FIntVector Expected; };
    const FHashCase Hashes[] = {
        { 0, 0, 0, FIntVector(7000, 6616, 52874) },
        { 1, 2, 3, FIntVector(26825, 11689, 49510) },
        { -1, 0, 0, FIntVector(36590, 25752, 191) },
        { -7, 11, -13, FIntVector(7994, 17175, 517) },
        { 123456, -654321, 42, FIntVector(31900, 11355, 10796) },
    };
    for (const FHashCase& Case : Hashes)
    {
        const FIntVector Got = WorldReliefNoise::Hash16(Case.X, Case.Y, Case.Z);
        TestTrue(FString::Printf(TEXT("Rand3DPCG16(%d, %d, %d) is %s (got %s)"), Case.X, Case.Y, Case.Z,
            *Case.Expected.ToString(), *Got.ToString()), Got == Case.Expected);
    }

    // -- The simplex and its gradient ---------------------------------------
    FVector3d Gradient;
    TestTrue(TEXT("simplex value at A"), Near(WorldReliefNoise::Simplex(A, Gradient), -0.3226424121942387));
    TestTrue(TEXT("simplex gradient at A"), Near(Gradient, FVector3d(3.5769516436543185, -0.9177807075555531, -0.24077926399999894)));
    TestTrue(TEXT("simplex value at B"), Near(WorldReliefNoise::Simplex(B, Gradient), 0.2897821367384506));
    TestTrue(TEXT("simplex gradient at B"), Near(Gradient, FVector3d(0.884187009374707, 0.9303530368734697, 2.14895895591946)));
    // The gradient is the value's own: central differences agree.
    WorldReliefNoise::Simplex(A, Gradient);
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        FVector3d Step = FVector3d::ZeroVector;
        Step[Axis] = 1.0e-6;
        FVector3d Unused;
        const double Difference = (WorldReliefNoise::Simplex(A + Step, Unused) - WorldReliefNoise::Simplex(A - Step, Unused)) / 2.0e-6;
        TestTrue(FString::Printf(TEXT("the simplex's gradient is its value's, axis %d (%.9f against %.9f)"), Axis, Gradient[Axis], Difference),
            FMath::Abs(Gradient[Axis] - Difference) < 1.0e-5);
    }

    // -- The coarse band's gradient noise, the Voronoi, the cell hash ---------
    TestTrue(TEXT("gradient noise at A"), Near(WorldReliefNoise::GradientNoise(A), 0.0846626387131395));
    TestTrue(TEXT("gradient noise at B"), Near(WorldReliefNoise::GradientNoise(B), 0.21445867956215167));
    const FVector4d AtA = WorldReliefNoise::Voronoi(A);
    TestTrue(TEXT("the site nearest A"), Near(FVector3d(AtA.X, AtA.Y, AtA.Z), FVector3d(-0.19378062431236706, 1.8322746763096283, -1.9640177066591178)));
    TestTrue(TEXT("and how far"), Near(AtA.W, 0.5630306720859443));
    const FVector4d AtB = WorldReliefNoise::Voronoi(B);
    TestTrue(TEXT("the site nearest B"), Near(FVector3d(AtB.X, AtB.Y, AtB.Z), FVector3d(11.920089334391037, -6.078764238258697, 89.23321217869884)));
    TestTrue(TEXT("and how far"), Near(AtB.W, 0.6270859959294708));
    TestTrue(TEXT("the cell hash"), Near(WorldReliefNoise::CellHash(FVector3d(3.7, -2.2, 11.9)), 0.04057373922331579));

    // -- The surface terms, the file's own bands ------------------------------
    const FFaceTerms Terms = WorldReliefNoise::FaceF64(Known, 0.0, Offset, 1.0);
    TestTrue(TEXT("the continent at D"), Near(Terms.Continent, -0.051624444675196335));
    TestTrue(TEXT("every detail band at D"), Near(Terms.Detail, -0.37006419577067096));
    TestTrue(TEXT("their slope at D"), Near(Terms.DetailSlope, FVector3d(-1.1190180843380462, -2.0437699869510126, -2.957997344868701)));
    TestTrue(TEXT("every crater band's albedo at D"), Near(Terms.CraterAlbedo, 0.1646827666094558));
    TestTrue(TEXT("and slope"), Near(Terms.CraterSlope, FVector3d(0.10226657986438999, 0.029989050621318493, 0.27960324776032125)));
    const FFaceTerms Faded = WorldReliefNoise::FaceF64(Known, 1.0 / 768.0, Offset, 1.0);
    TestTrue(TEXT("at a footprint of 1/768: the continent"), Near(Faded.Continent, -0.052906497740322966));
    TestTrue(TEXT("the detail"), Near(Faded.Detail, -0.028110410395840155));
    TestTrue(TEXT("its slope"), Near(Faded.DetailSlope, FVector3d(-0.18906468101433271, 0.3914639024500385, -2.955562449468286)));
    TestTrue(TEXT("the craters' albedo"), Near(Faded.CraterAlbedo, 0.015437686430484467));
    TestTrue(TEXT("and slope"), Near(Faded.CraterSlope, FVector3d(0.02537765447267675, -0.04054271510388996, 0.01898122575896894)));
    const FFaceTerms Giant = WorldReliefNoise::FaceF64(Known, 1.0 / 768.0, Offset, 6.0);
    TestTrue(TEXT("a giant's stretched continent"), Near(Giant.Continent, 0.2988758606069588));
    TestTrue(TEXT("its stretched detail"), Near(Giant.Detail, -0.08159535029992765));
    TestTrue(TEXT("and slope"), Near(Giant.DetailSlope, FVector3d(-0.6215217207242866, 0.5170705707138437, 0.4500212151402112)));
    TestTrue(TEXT("craters are never stretched"), Near(Giant.CraterAlbedo, Faded.CraterAlbedo) && Near(Giant.CraterSlope, Faded.CraterSlope));

    // -- The height: every detail band the GPU draws and the four finer the
    //    C++ alone carries on an Earth, over the measured S_max, at 6 km.
    //    Worked by the planner over the proven bound (-39163.893022613505,
    //    gradient (-1868268.5494285857, -1201146.9951713625,
    //    -10114520.605642153), and -37499.20596158043 at R/768), then scaled
    //    by DetailBound / SMaxMeasured = 0.13266826582782978 /
    //    0.029932993600784347: S / S_max is -0.289 here, under the knee,
    //    where PeakCap is the identity. -----------------------------------------
    const FWorldRelief Relief(Earthlike());
    FVector3d HeightGradient;
    TestTrue(TEXT("the height at D"), FMath::Abs(Relief.HeightAndGradient(Known, HeightGradient) - (-173581.227446647)) < 1.0e-4);
    TestTrue(TEXT("and its gradient in D"),
        (HeightGradient - FVector3d(-8280493.152775431, -5323693.679923737, -44829325.33668244)).GetAbsMax() < 1.0e-2);
    TestTrue(TEXT("the height at D with a footprint of R/768"), FMath::Abs(Relief.Height(Known, 6.3781e8 / 768.0) - (-166203.04307662472)) < 1.0e-4);
    return true;
}

/**
 * The engine's own /Project mapping finds the shared file (the developer's
 * ruling at R2). UE 5.8's FEngineLoop::PreInit maps /Project to
 * <project>/Shaders whenever that directory exists, before any module loads
 * (LaunchEngineLoop.cpp), so the project maps nothing itself: the
 * DeepSpaceShaders module that once did is gone, and this holds that it is
 * gone and that the engine's mapping is the one M_SkyBody's include meets.
 */
bool FShaderMappingTest::RunTest(const FString& Parameters)
{
    // No project module maps /Project: DeepSpaceShaders is out of the
    // descriptor and never loaded. (Not ModuleExists: UBT keeps a removed
    // module's line in Binaries' manifest until a clean build.)
    FString Descriptor;
    TSharedPtr<FJsonObject> Project;
    TestTrue(TEXT("DeepSpace.uproject reads"), FFileHelper::LoadFileToString(Descriptor, *FPaths::GetProjectFilePath())
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Descriptor), Project) && Project.IsValid());
    TArray<FString> Modules;
    if (Project.IsValid())
    {
        for (const TSharedPtr<FJsonValue>& Module : Project->GetArrayField(TEXT("Modules")))
        {
            Modules.Add(Module->AsObject()->GetStringField(TEXT("Name")));
        }
    }
    TestEqual(TEXT("the project's one module is DeepSpace: DeepSpaceShaders is removed"), FString::Join(Modules, TEXT(", ")), FString(TEXT("DeepSpace")));
    TestFalse(TEXT("and it is not loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("DeepSpaceShaders")));
    const FString* Mapped = AllShaderSourceDirectoryMappings().Find(TEXT("/Project"));
    if (!TestNotNull(TEXT("the engine maps /Project at PreInit, before any shader compiles"), Mapped))
    {
        return false;
    }
    TestEqual(TEXT("to this project's Shaders/"), FPaths::ConvertRelativePathToFull(*Mapped),
        FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"))));
    const FString Real = GetShaderSourceFilePath(SkyMaterial::WorldReliefInclude);
    TestTrue(FString::Printf(TEXT("and it finds %s, a file (%s)"), SkyMaterial::WorldReliefInclude, *Real), FPaths::FileExists(Real));
    TestEqual(TEXT("the very file the C++ compiles"), FPaths::ConvertRelativePathToFull(Real),
        FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders"), TEXT("Private"), TEXT("WorldRelief.ush"))));
    return true;
}

bool FWorldReliefDeterministicTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief One(Earthlike());
    const FWorldRelief Two(Earthlike());
    FWorldReliefParams Elsewhere = Earthlike();
    Elsewhere.SeedOffset += FVector3d(1.0 / 256.0, 0.0, 0.0);
    const FWorldRelief Other(Elsewhere);
    int32 Mismatched = 0;
    int32 Moved = 0;
    for (const FVector3d& D : Directions(1000, 7))
    {
        Mismatched += (One.Height(D) == Two.Height(D) && One.Height(D, 1.0e5) == Two.Height(D, 1.0e5)) ? 0 : 1;
        Moved += One.Height(D) != Other.Height(D) ? 1 : 0;
    }
    TestEqual(TEXT("one world's params make one ground, bit for bit"), Mismatched, 0);
    TestTrue(FString::Printf(TEXT("and the next seed offset another ground (%d of 1,000 differ)"), Moved), Moved >= 990);
    return true;
}

bool FWorldReliefSeedMatchesSkyTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();
    const FGalaxyGenerator Galaxy(20260925, Priors);
    const FStarSystem Home = FStarSystemGenerator::Generate(Galaxy.GenerateSector(FInt64Vector(-1, -1, 0))[0], Priors);
    const FSkySystem Sky = FSkySystem::FromSystem(Home, {});
    for (int32 Index = 1; Index < Sky.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = Sky.Bodies[Index];
        const FLinearColor Gpu = ShipSky::SurfaceSeed(Body.SurfaceSeed, Body.BeltPairs);
        const FVector3d Drawn(Gpu.R, Gpu.G, Gpu.B);
        const FWorldRelief Relief(Body.Relief);
        TestTrue(FString::Printf(TEXT("%s's ground is taken from the noise its face is: the float M_SkyBody gets, exactly"), *Body.Id.ToString()),
            Relief.GetParams().SeedOffset == Drawn);
        TestEqual(FString::Printf(TEXT("%s's Face is the terms at that offset"), *Body.Id.ToString()),
            Relief.Face(Known, 0.0).Detail, WorldReliefNoise::FaceF64(Known, 0.0, Drawn, 1.0).Detail);
    }
    return true;
}

bool FWorldReliefBoundsTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    TestEqual(TEXT("MaxHeightCm is exactly the peak"), Relief.MaxHeightCm(), 6.0e5);
    TestEqual(TEXT("and MinHeightCm its negative"), Relief.MinHeightCm(), -6.0e5);
    // The developer's ruling on planning note 3: normalised by the measured
    // maximum under a smooth hard cap, the height never passes the peak on a
    // dense sweep, and the drawn peaks reach at least 0.9 of it.
    int32 Outside = 0;
    double Highest = 0.0;
    double NoiseHighest = 0.0;
    double NoiseSteepest = 0.0;
    for (const FVector3d& D : Directions(200000, 11))
    {
        const double Height = Relief.Height(D);
        Outside += (Height > Relief.MaxHeightCm() || Height < Relief.MinHeightCm()) ? 1 : 0;
        Highest = FMath::Max(Highest, FMath::Abs(Height));
        FVector3d NoiseGradient;
        NoiseHighest = FMath::Max(NoiseHighest, FMath::Abs(WorldReliefNoise::Simplex(D * 97.0, NoiseGradient)));
        NoiseSteepest = FMath::Max(NoiseSteepest, NoiseGradient.Size());
    }
    TestEqual(TEXT("no sample of 200,000 lies outside [MinHeightCm, MaxHeightCm]"), Outside, 0);
    TestTrue(FString::Printf(TEXT("and the drawn peaks reach at least 0.9 of the peak (%.3f)"), Highest / Relief.MaxHeightCm()),
        Highest >= 0.9 * Relief.MaxHeightCm());
    TestTrue(TEXT("the simplex never exceeds its value bound"), NoiseHighest <= WorldReliefNoise::SimplexValueBound);
    TestTrue(TEXT("nor its gradient bound"), NoiseSteepest <= WorldReliefNoise::SimplexGradientBound);
    // S_max is a measured maximum, so somewhere S passes it: found by a wider
    // search (1,024 worlds x 1,024 directions, FRandomStream(777)), 1.114
    // S_max. The cap holds it under the peak.
    {
        FWorldReliefParams Past = Earthlike();
        Past.SeedOffset = FVector3d(65.234375, 147.68359375, 13.6484375);
        const FWorldRelief Beyond(Past);
        const FVector3d Where(0.40159888190583304, 0.51100376441368989, 0.75999571762413431);
        const double Share = FMath::Abs(Beyond.DetailSum(Where, 0.0)) / Beyond.SMax();
        TestTrue(FString::Printf(TEXT("a sample past the measured maximum (%.3f S_max)"), Share), Share > 1.1);
        const double Height = FMath::Abs(Beyond.Height(Where));
        TestTrue(FString::Printf(TEXT("is held under the peak by the cap (%.4f of it)"), Height / Beyond.MaxHeightCm()),
            Height < Beyond.MaxHeightCm() && Height > 0.98 * Beyond.MaxHeightCm());
    }
    AddInfo(FString::Printf(TEXT("highest of 200,000: %.3f of the peak; the simplex reached %.4f of %.4f and %.3f of %.3f (proven bounds, not tight)"),
        Highest / Relief.MaxHeightCm(), NoiseHighest, WorldReliefNoise::SimplexValueBound, NoiseSteepest, WorldReliefNoise::SimplexGradientBound));

    FWorldReliefParams Ocean = Earthlike();
    Ocean.PeakCm = 0.0;
    Ocean.Ground = EGround::None;
    const FWorldRelief Flat(Ocean);
    int32 Raised = 0;
    for (const FVector3d& D : Directions(1000, 13))
    {
        Raised += Flat.Height(D) == 0.0 ? 0 : 1;
    }
    TestEqual(TEXT("an ocean's ground is its datum everywhere"), Raised, 0);
    TestEqual(TEXT("with no height"), Flat.MaxHeightCm(), 0.0);
    TestEqual(TEXT("and no slope"), Flat.MaxSlope(), 0.0);
    return true;
}

bool FWorldReliefGradientTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    constexpr double Step = 1.0e-9;
    int32 Wrong = 0;
    for (const double FootprintCm : { 0.0, 6.3781e8 / 3072.0 })
    {
        for (const FVector3d& D : Directions(1000, 17))
        {
            FVector3d Gradient;
            Relief.HeightAndGradient(D, Gradient, FootprintCm);
            const FVector3d Across = Tangent(D);
            for (const FVector3d& Along : { Across, FVector3d::CrossProduct(D, Across) })
            {
                const double Difference = (Relief.Height(D + Along * Step, FootprintCm) - Relief.Height(D - Along * Step, FootprintCm)) / (2.0 * Step);
                const double Analytic = FVector3d::DotProduct(Gradient, Along);
                Wrong += FMath::Abs(Difference - Analytic) <= 1.0e-4 * Gradient.Size() + 1.0 ? 0 : 1;
            }
        }
    }
    TestEqual(TEXT("the analytic gradient is the height's own, every band and every fade"), Wrong, 0);
    return true;
}

bool FWorldReliefBandLimitTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Earth(Earthlike());
    const TConstArrayView<double> Bands = Earth.GetDetailFrequencies();
    TestEqual(TEXT("an Earth carries the GPU's twelve bands and four finer"), Bands.Num(), 16);
    TestTrue(FString::Printf(TEXT("nothing finer than 5 m (%.1f cm)"), Earth.FinestWavelengthCm()), Earth.FinestWavelengthCm() >= FWorldRelief::BandLimitCm);
    TestTrue(TEXT("and as fine as it may go: one octave more would be under 5 m"),
        Earth.GetParams().RadiusCm / (2.0 * Bands.Last()) < FWorldRelief::BandLimitCm);
    for (int32 Band = 12; Band < Bands.Num(); ++Band)
    {
        TestEqual(FString::Printf(TEXT("finer band %d is an octave above the last"), Band + 1), Bands[Band], 2.0 * Bands[Band - 1]);
    }
    FWorldReliefParams Small = Earthlike();
    Small.RadiusCm = 6.3781e8 * FMath::Pow(0.05, 0.28);
    const FWorldRelief Light(Small);
    TestEqual(TEXT("the smallest rocky world procgen makes carries three finer"), Light.GetDetailFrequencies().Num(), 15);
    TestTrue(TEXT("and nothing finer than 5 m either"), Light.FinestWavelengthCm() >= FWorldRelief::BandLimitCm);
    return true;
}

bool FWorldReliefFootprintTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    const double Radius = Relief.GetParams().RadiusCm;
    TestEqual(TEXT("a footprint of 0 omits nothing"), Relief.OmittedBoundCm(0.0), 0.0);
    int32 Beyond = 0;
    for (const double FootprintCm : { 1000.0, Radius / 12288.0, Radius / 768.0, Radius / 24.0 })
    {
        for (const FVector3d& D : Directions(10000, 19))
        {
            const double Removed = FMath::Abs(Relief.Height(D) - Relief.Height(D, FootprintCm));
            Beyond += Removed <= Relief.OmittedBoundCm(FootprintCm) + 1.0e-9 ? 0 : 1;
        }
    }
    TestEqual(TEXT("what a footprint removes never exceeds OmittedBoundCm"), Beyond, 0);
    int32 Left = 0;
    for (const FVector3d& D : Directions(1000, 23))
    {
        Left += Relief.Height(D, Radius / 24.0) == 0.0 ? 0 : 1;
    }
    TestEqual(TEXT("a footprint of R/24 fades every band away, to the datum"), Left, 0);
    TestTrue(TEXT("and then OmittedBoundCm is the whole peak"),
        FMath::IsNearlyEqual(Relief.OmittedBoundCm(Radius / 24.0), Relief.MaxHeightCm(), 1.0e-9 * Relief.MaxHeightCm()));
    return true;
}

bool FWorldReliefSlopeBoundTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    const FWorldRelief Relief(Earthlike());
    int32 Steeper = 0;
    double Steepest = 0.0;
    for (const FVector3d& D : Directions(100000, 29))
    {
        FVector3d Gradient;
        Relief.HeightAndGradient(D, Gradient);
        const FVector3d AlongGround = Gradient - FVector3d::DotProduct(Gradient, D) * D;
        const double Slope = AlongGround.Size() / Relief.GetParams().RadiusCm;
        Steeper += Slope <= Relief.MaxSlope() ? 0 : 1;
        Steepest = FMath::Max(Steepest, Slope);
    }
    TestEqual(TEXT("no sample of 100,000 is steeper than MaxSlope"), Steeper, 0);
    AddInfo(FString::Printf(TEXT("steepest %.4f against the bound %.4f (cm per cm)"), Steepest, Relief.MaxSlope()));
    return true;
}

bool FWorldReliefMeasuredMaxTest::RunTest(const FString& Parameters)
{
    // SMaxMeasured's own recipe, sample for sample (its comment in
    // WorldRelief.h): a noise, band or weight that moves fails here first.
    FRandomStream Stream(20260927);
    double Measured = 0.0;
    double LeastWorld = TNumericLimits<double>::Max();
    for (int32 World = 0; World < 256; ++World)
    {
        FWorldReliefParams Params;
        Params.SeedOffset = FVector3d(Stream.RandRange(0, 65535), Stream.RandRange(0, 65535), Stream.RandRange(0, 65535)) / 256.0;
        Params.RadiusCm = 6.3781e8;
        Params.PeakCm = 6.0e5;
        Params.Ground = EGround::Solid;
        const FWorldRelief Relief(Params);
        double WorldMax = 0.0;
        for (int32 Sample = 0; Sample < 1024; ++Sample)
        {
            WorldMax = FMath::Max(WorldMax, FMath::Abs(Relief.DetailSum(FVector3d(Stream.GetUnitVector()), 0.0)));
        }
        Measured = FMath::Max(Measured, WorldMax);
        LeastWorld = FMath::Min(LeastWorld, WorldMax);
    }
    TestTrue(FString::Printf(TEXT("SMaxMeasured is the maximum of its 262,144 samples (%.17g against %.17g)"), Measured, FWorldRelief::SMaxMeasured),
        FMath::IsNearlyEqual(Measured, FWorldRelief::SMaxMeasured, 1.0e-12 * FWorldRelief::SMaxMeasured));
    const FWorldRelief Earth([] { FWorldReliefParams P; P.RadiusCm = 6.3781e8; P.PeakCm = 6.0e5; P.Ground = EGround::Solid; return P; }());
    TestEqual(TEXT("the samples were an Earth's sixteen bands"), Earth.GetDetailFrequencies().Num(), 16);
    TestTrue(TEXT("and the proven bound lies above the measured maximum"), Earth.DetailBound() > FWorldRelief::SMaxMeasured);
    AddInfo(FString::Printf(TEXT("S_max %.6f; the proven bound %.6f is %.2f times it; the least of 256 worlds' 1,024-sample maxima is %.3f of it"),
        Measured, Earth.DetailBound(), Earth.DetailBound() / Measured, LeastWorld / Measured));
    return true;
}

bool FWorldReliefPeakCapTest::RunTest(const FString& Parameters)
{
    using namespace WorldReliefTestLocal;
    // The identity to the knee, value and slope.
    for (const double X : { 0.0, 0.25, -0.5, 0.8, -0.8 })
    {
        double Slope = 0.0;
        TestEqual(FString::Printf(TEXT("PeakCap(%.2f) is itself, under the knee"), X), FWorldRelief::PeakCap(X, &Slope), X);
        TestEqual(FString::Printf(TEXT("with slope 1 (%.2f)"), X), Slope, 1.0);
    }
    // Worked by hand: 0.8 + 0.2 tanh(1) and 0.8 + 0.2 tanh(0.5); slope 1 - tanh(1)^2.
    double SlopeAtOne = 0.0;
    TestTrue(TEXT("at the measured maximum it is 0.8 + 0.2 tanh(1)"), FMath::Abs(FWorldRelief::PeakCap(1.0, &SlopeAtOne) - 0.952318831191153) < 1.0e-12);
    TestTrue(TEXT("with slope 1 - tanh(1)^2"), FMath::Abs(SlopeAtOne - 0.41997434161402614) < 1.0e-12);
    TestTrue(TEXT("at 0.9, 0.8 + 0.2 tanh(0.5)"), FMath::Abs(FWorldRelief::PeakCap(0.9) - 0.892423431452002) < 1.0e-12);
    TestEqual(TEXT("and odd"), FWorldRelief::PeakCap(-1.0), -FWorldRelief::PeakCap(1.0));
    // Smooth across the knee, and its slope the value's own.
    constexpr double Step = 1.0e-7;
    double Previous = FWorldRelief::PeakCap(0.0);
    int32 Rough = 0;
    int32 Unmonotone = 0;
    for (double X = Step; X < 6.0; X += 1.0e-3)
    {
        double Slope = 0.0;
        const double Value = FWorldRelief::PeakCap(X, &Slope);
        const double Difference = (FWorldRelief::PeakCap(X + Step) - FWorldRelief::PeakCap(X - Step)) / (2.0 * Step);
        Rough += FMath::Abs(Difference - Slope) < 1.0e-6 ? 0 : 1;
        Unmonotone += Value >= Previous ? 0 : 1;
        Previous = Value;
    }
    TestEqual(TEXT("its slope is its value's own, across the knee"), Rough, 0);
    TestEqual(TEXT("and it never falls"), Unmonotone, 0);
    // The guarantee: whatever S the proof allows maps under the peak.
    const FWorldRelief Relief(Earthlike());
    const double Worst = Relief.DetailBound() / Relief.SMax();
    TestTrue(FString::Printf(TEXT("at the proven worst, %.2f S_max, it is still at most 1"), Worst), FWorldRelief::PeakCap(Worst) <= 1.0);
    TestTrue(TEXT("and far past it too"), FWorldRelief::PeakCap(1.0e6) <= 1.0 && FWorldRelief::PeakCap(-1.0e6) >= -1.0);
    return true;
}

#endif
