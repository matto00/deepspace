#include "Misc/AutomationTest.h"
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

#endif
