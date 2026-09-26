#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/StringOutputDevice.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FUniverseSubsystemTest,
    "DeepSpace.Universe.Subsystem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Named, never anonymous: the unity build pastes every test file into one
// translation unit, where each file's anonymous namespace is the same one
// and a second SameStub is a redefinition.
namespace UniverseSubsystemTestLocal
{
    /** A throwaway game world: world subsystems are created with it, which is
     *  the only way to get a real UUniverseSubsystem. It never begins play --
     *  every query must work without it. */
    struct FTestWorld
    {
        UWorld* World = nullptr;

        explicit FTestWorld(const TCHAR* Name)
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, Name);
            FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
            Context.SetCurrentWorld(World);
        }

        ~FTestWorld()
        {
            GEngine->DestroyWorldContext(World);
            World->DestroyWorld(false);
        }
    };
}

bool FUniverseSubsystemTest::RunTest(const FString& Parameters)
{
    using namespace UniverseSubsystemTestLocal;

    // -- -UniverseSeed= is read by hand, because a Config property is not ------
    {
        TestEqual(TEXT("no flag: the configured seed"), UUniverseSubsystem::ResolveSeed(20260925, TEXT("-nullrhi -unattended")), uint64(20260925));
        TestEqual(TEXT("decimal"), UUniverseSubsystem::ResolveSeed(20260925, TEXT("-nullrhi -UniverseSeed=42 -unattended")), uint64(42));
        TestEqual(TEXT("hex"), UUniverseSubsystem::ResolveSeed(20260925, TEXT("-UniverseSeed=0xDEADBEEF")), uint64(0xDEADBEEF));
        TestEqual(TEXT("the full 64 bits"), UUniverseSubsystem::ResolveSeed(1, TEXT("-UniverseSeed=18446744073709551615")), ~uint64(0));
        TestEqual(TEXT("garbage falls back to the configured seed"), UUniverseSubsystem::ResolveSeed(7, TEXT("-UniverseSeed=kessa")), uint64(7));
        TestEqual(TEXT("a negative configured seed is its two's complement"), UUniverseSubsystem::ResolveSeed(-1, TEXT("")), ~uint64(0));
        TestEqual(TEXT("no command line at all"), UUniverseSubsystem::ResolveSeed(5, nullptr), uint64(5));
    }

    FTestWorld First(TEXT("UniverseTestWorldA"));
    UUniverseSubsystem* Universe = First.World->GetSubsystem<UUniverseSubsystem>();
    if (!TestNotNull(TEXT("the world has a universe subsystem"), Universe))
    {
        return false;
    }
    TestEqual(TEXT("Get finds it through the world"), UUniverseSubsystem::Get(First.World), Universe);

    // -- the seed is DefaultGame.ini's, carried through the Config property ----
    // The C++ default is zero so that a section that never loads, or an int64
    // Config property that does not read, cannot pass for the real seed.
    {
        int64 IniSeed = 0;
        const bool bInIni = GConfig->GetInt64(TEXT("/Script/DeepSpace.UniverseSubsystem"), TEXT("UniverseSeed"), IniSeed, GGameIni);
        TestTrue(TEXT("DefaultGame.ini sets UniverseSeed in the subsystem's section"), bInIni);
        TestNotEqual(TEXT("and it is not the C++ default"), IniSeed, int64(0));

        const FInt64Property* Property = FindFProperty<FInt64Property>(UUniverseSubsystem::StaticClass(), TEXT("UniverseSeed"));
        if (TestNotNull(TEXT("UniverseSeed is a reflected int64"), Property))
        {
            TestEqual(TEXT("the class default read the ini's seed"),
                Property->GetPropertyValue_InContainer(GetDefault<UUniverseSubsystem>()), IniSeed);
            TestEqual(TEXT("and so did the live subsystem"), Property->GetPropertyValue_InContainer(Universe), IniSeed);
        }

        TestEqual(TEXT("the root seed is the configured one, resolved against this command line"),
            Universe->GetRootSeed(), UUniverseSubsystem::ResolveSeed(IniSeed, FCommandLine::Get()));
        if (!FString(FCommandLine::Get()).Contains(TEXT("UniverseSeed=")))
        {
            TestEqual(TEXT("with no -UniverseSeed=, the root seed is the ini's"), Universe->GetRootSeed(), static_cast<uint64>(IniSeed));
        }
    }

    const FGalaxyGenerator Reference(Universe->GetRootSeed(), Universe->GetPriors());

    // -- every query answers before begin-play ---------------------------------
    const FSystemId Start = Universe->GetStartSystem();
    TestTrue(TEXT("the start system is the generator's, before begin-play"), Start == Reference.StartSystem());

    const TOptional<FStarSystem> Home = Universe->GetSystem(Start);
    if (!TestTrue(TEXT("the start system exists"), Home.IsSet()))
    {
        return false;
    }
    TestTrue(TEXT("and has a planet"), Home->Planets.Num() > 0);

    // "Which system am I in" is a question about a position.
    {
        const TOptional<FStarSystem> AtStar = Universe->GetSystemAt(Home->Stub.Position);
        TestTrue(TEXT("the start star's position is in the start system"), AtStar.IsSet() && AtStar->Stub.Id == Start);

        const TOptional<FStarSystem> AtPlanet = Universe->GetSystemAt(Home->PlanetPosition(0));
        TestTrue(TEXT("its first planet is in the start system"), AtPlanet.IsSet() && AtPlanet->Stub.Id == Start);

        const FUniversePosition Edge = Home->Stub.Position + FVector(0.0, 0.0, 0.9 * FStarSystem::InSystemRadiusCm);
        const TOptional<FStarSystem> NearEdge = Universe->GetSystemAt(Edge);
        TestTrue(TEXT("just inside the system's edge is still in it"), NearEdge.IsSet() && NearEdge->Stub.Id == Start);

        // Half-way to the nearest neighbour is between stars.
        const TArray<FStarSystemStub> Near = Universe->GetSystemsNear(Home->Stub.Position, 20.0 * UniverseUnits::CmPerLightYear);
        if (TestTrue(TEXT("the start system has neighbours"), Near.Num() >= 2))
        {
            TestTrue(TEXT("nearest first: the start system is first near itself"), Near[0].Id == Start);
            const FVector ToNext = Near[1].Position - Home->Stub.Position;
            if (ToNext.Size() > 1.0 * UniverseUnits::CmPerLightYear)
            {
                TestFalse(TEXT("half-way to the nearest neighbour is in no system"),
                    Universe->GetSystemAt(Home->Stub.Position + ToNext * 0.5).IsSet());
            }
        }
    }

    TestFalse(TEXT("a slot the sector lacks is no system"),
        Universe->GetSystem(FSystemId{Start.Sector, Reference.SystemCount(Start.Sector)}).IsSet());

    // -- two worlds with the same seed agree ------------------------------------
    {
        FTestWorld Second(TEXT("UniverseTestWorldB"));
        const UUniverseSubsystem* Other = Second.World->GetSubsystem<UUniverseSubsystem>();
        if (TestNotNull(TEXT("the second world has a universe too"), Other))
        {
            TestEqual(TEXT("the same root seed"), Other->GetRootSeed(), Universe->GetRootSeed());
            TestTrue(TEXT("the same start system"), Other->GetStartSystem() == Start);
            const TOptional<FStarSystem> Again = Other->GetSystem(Start);
            bool bSame = Again.IsSet() && Again->Stub.Name == Home->Stub.Name && Again->Planets.Num() == Home->Planets.Num();
            for (int32 I = 0; bSame && I < Home->Planets.Num(); ++I)
            {
                bSame = Again->Planets[I].SemiMajorAxisAU == Home->Planets[I].SemiMajorAxisAU
                    && Again->Planets[I].MassEarth == Home->Planets[I].MassEarth
                    && Again->Planets[I].Designation == Home->Planets[I].Designation;
            }
            TestTrue(TEXT("and the same planets in it"), bSame);
        }
    }

    // -- the console commands answer, with no ship and no begin-play ------------
    {
        FStringOutputDevice Out;
        Out.SetAutoEmitLineTerminator(true);
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Universe.Describe"), Out, First.World);
        TestTrue(TEXT("ds.Universe.Describe names the start system"), Out.Contains(Home->Stub.Name));
        TestTrue(TEXT("and designates its planets"), Out.Contains(Home->Planets[0].Designation));
        // For reading: whether home reads as a place or as a roll.
        AddInfo(TEXT("ds.Universe.Describe:\n") + Out);

        FStringOutputDevice Near;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Universe.Near 12"), Near, First.World);
        TestTrue(TEXT("ds.Universe.Near lists the neighbourhood"), Near.Contains(TEXT("systems within 12.0 ly")));

        FStringOutputDevice Elsewhere;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Universe.Describe 1"), Elsewhere, First.World);
        const FGalaxyGenerator Seed1(1, Universe->GetPriors());
        const TOptional<FStarSystemStub> Seed1Home = Seed1.GenerateStub(Seed1.StartSystem());
        TestTrue(TEXT("ds.Universe.Describe <seed> describes that universe's start"),
            Seed1Home.IsSet() && Elsewhere.Contains(Seed1Home->Name));

        FStringOutputDevice ById;
        const FString Command = FString::Printf(TEXT("ds.Universe.Describe %llu %lld %lld %lld %d"),
            static_cast<unsigned long long>(Universe->GetRootSeed()), static_cast<long long>(Start.Sector.X),
            static_cast<long long>(Start.Sector.Y), static_cast<long long>(Start.Sector.Z), Start.Slot);
        IConsoleManager::Get().ProcessUserConsoleInput(*Command, ById, First.World);
        TestTrue(TEXT("ds.Universe.Describe <seed> <x> <y> <z> <slot> describes that system"), ById.Contains(Home->Stub.Name));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
