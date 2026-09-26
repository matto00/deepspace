#include "Universe/UniverseSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/GalaxyGenerator.h"
#include "Universe/ProcGenPriorsConfig.h"
#include "Universe/SystemDescription.h"
#include "Universe/UniverseUnits.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverse, Log, All);

namespace
{
    /** A seed as a person types it: decimal, negative decimal (two's
     *  complement, as the int64 config property stores it), or 0x hex. */
    bool ParseSeed(const FString& Text, uint64& OutSeed)
    {
        const FString Trimmed = Text.TrimStartAndEnd();
        if (Trimmed.IsEmpty())
        {
            return false;
        }
        TCHAR* End = nullptr;
        const uint64 Value = Trimmed.StartsWith(TEXT("-"))
            ? static_cast<uint64>(FCString::Strtoi64(*Trimmed, &End, 10))
            : FCString::Strtoui64(*Trimmed, &End, 0);
        if (End == *Trimmed || *End != TEXT('\0'))
        {
            return false;
        }
        OutSeed = Value;
        return true;
    }

    /** Where the ship is, if this world has one. */
    TOptional<FUniversePosition> ShipPosition(const UWorld* World)
    {
        if (const UShipSubsystem* Ship = UShipSubsystem::Get(World))
        {
            return Ship->GetFlightState().GetUniversePosition();
        }
        return {};
    }

    void PrintLines(FOutputDevice& Out, const FString& Text)
    {
        TArray<FString> Lines;
        Text.ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);
        for (const FString& Line : Lines)
        {
            Out.Log(Line);
        }
    }

    void Describe(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(World);
        // Another universe's seed is described under this session's priors:
        // the ini's, whether or not this world has a universe of its own.
        const FGenPriors Priors = GetDefault<UProcGenPriorsConfig>()->ToPriors();

        if (Args.IsEmpty())
        {
            if (!Universe)
            {
                Out.Log(TEXT("ds.Universe.Describe: no universe in this world. Give a seed: ds.Universe.Describe <seed> [x y z slot]"));
                return;
            }
            const TOptional<FUniversePosition> Where = ShipPosition(World);
            TOptional<FStarSystem> System = Where ? Universe->GetSystemAt(*Where) : TOptional<FStarSystem>();
            if (!System)
            {
                Out.Log(Where ? TEXT("The ship is between stars; the start system:") : TEXT("No ship; the start system:"));
                System = Universe->GetSystem(Universe->GetStartSystem());
            }
            if (System)
            {
                PrintLines(Out, SystemDescription::Describe(*System));
            }
            return;
        }

        uint64 Seed = 0;
        if (!ParseSeed(Args[0], Seed))
        {
            Out.Logf(TEXT("ds.Universe.Describe: '%s' is not a seed"), *Args[0]);
            return;
        }
        const FGalaxyGenerator Galaxy(Seed, Priors);

        FSystemId Id;
        if (Args.Num() >= 5)
        {
            Id.Sector = FInt64Vector(FCString::Atoi64(*Args[1]), FCString::Atoi64(*Args[2]), FCString::Atoi64(*Args[3]));
            Id.Slot = FCString::Atoi(*Args[4]);
        }
        else
        {
            Id = Galaxy.StartSystem();
        }

        if (const TOptional<FStarSystemStub> Stub = Galaxy.GenerateStub(Id))
        {
            PrintLines(Out, SystemDescription::Describe(Galaxy.GenerateSystem(*Stub)));
        }
        else
        {
            Out.Logf(TEXT("ds.Universe.Describe: universe 0x%016llX has no system at sector (%lld, %lld, %lld) slot %d"),
                static_cast<unsigned long long>(Seed), static_cast<long long>(Id.Sector.X),
                static_cast<long long>(Id.Sector.Y), static_cast<long long>(Id.Sector.Z), Id.Slot);
        }
    }

    void Near(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        const UUniverseSubsystem* Universe = UUniverseSubsystem::Get(World);
        if (!Universe)
        {
            Out.Log(TEXT("ds.Universe.Near: no universe in this world"));
            return;
        }

        const double RangeLy = Args.IsEmpty() ? 12.0 : FCString::Atod(*Args[0]);
        const TOptional<FUniversePosition> Ship = ShipPosition(World);
        FUniversePosition From;
        if (Ship)
        {
            From = *Ship;
        }
        else if (const TOptional<FStarSystem> Start = Universe->GetSystem(Universe->GetStartSystem()))
        {
            Out.Log(TEXT("No ship; measuring from the start system."));
            From = Start->Stub.Position;
        }

        const TArray<FStarSystemStub> Stubs = Universe->GetSystemsNear(From, RangeLy * UniverseUnits::CmPerLightYear);
        Out.Logf(TEXT("%d systems within %.1f ly:"), Stubs.Num(), RangeLy);
        for (const FStarSystemStub& Stub : Stubs)
        {
            Out.Log(SystemDescription::DescribeStub(Stub, From.DistanceTo(Stub.Position)));
        }
    }

    /** Re-reads the priors from DefaultGame.ini on disk and says which moved.
     *  Nothing needs telling: every query asks the config afresh, so the next
     *  Describe, the next chart and the sky's next frame are the new universe.
     *  The ship does not move -- it is wherever it was, in whatever the new
     *  numbers put there. */
    void ReloadPriors(const TArray<FString>& Args, UWorld* World, FOutputDevice& Out)
    {
        const FGenPriors Before = GetDefault<UProcGenPriorsConfig>()->ToPriors();
        UProcGenPriorsConfig::ReloadFromIni();
        const FGenPriors After = GetDefault<UProcGenPriorsConfig>()->ToPriors();

        int32 Changed = 0;
#define DS_REPORT_PRIOR(Name)                                                                   \
        if (Before.Name != After.Name)                                                          \
        {                                                                                       \
            Out.Logf(TEXT("  %s  %.6g -> %.6g"), TEXT(#Name), Before.Name, After.Name);         \
            ++Changed;                                                                          \
        }
        DS_GEN_PRIORS(DS_REPORT_PRIOR)
#undef DS_REPORT_PRIOR

        if (Changed == 0)
        {
            Out.Log(TEXT("ds.Universe.ReloadPriors: DefaultGame.ini's priors are the ones already in use."));
            return;
        }
        Out.Logf(TEXT("ds.Universe.ReloadPriors: %d prior%s changed; the universe has re-rolled around the ship. ")
                 TEXT("ds.Universe.Describe to read where it now is."),
            Changed, Changed == 1 ? TEXT("") : TEXT("s"));
        UE_LOG(LogUniverse, Log, TEXT("Priors reloaded from the ini: %d changed"), Changed);
    }

    FAutoConsoleCommandWithWorldArgsAndOutputDevice DescribeCommand(
        TEXT("ds.Universe.Describe"),
        TEXT("Describe the system the ship is in, or else the start system. ")
        TEXT("'ds.Universe.Describe <seed>' describes another universe's start system; ")
        TEXT("'ds.Universe.Describe <seed> <x> <y> <z> <slot>' any system of it."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Describe));

    FAutoConsoleCommandWithWorldArgsAndOutputDevice NearCommand(
        TEXT("ds.Universe.Near"),
        TEXT("'ds.Universe.Near <ly>': the systems within that range of the ship, nearest first (default 12 ly)."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&Near));

    FAutoConsoleCommandWithWorldArgsAndOutputDevice ReloadPriorsCommand(
        TEXT("ds.Universe.ReloadPriors"),
        TEXT("Re-read the procgen priors from Config/DefaultGame.ini ([/Script/DeepSpace.ProcGenPriorsConfig]) ")
        TEXT("and list the ones that changed. No rebuild, no restart."),
        FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&ReloadPriors));
}

UUniverseSubsystem* UUniverseSubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext)
    {
        return nullptr;
    }
    const UWorld* World = WorldContext->GetWorld();
    return World ? World->GetSubsystem<UUniverseSubsystem>() : nullptr;
}

void UUniverseSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    RootSeed = ResolveSeed(UniverseSeed, FCommandLine::Get());

    UE_LOG(LogUniverse, Log, TEXT("Universe seed %llu (0x%016llX)"),
        static_cast<unsigned long long>(RootSeed), static_cast<unsigned long long>(RootSeed));
}

uint64 UUniverseSubsystem::GetRootSeed() const
{
    return RootSeed;
}

FGenPriors UUniverseSubsystem::GetPriors() const
{
    return GetDefault<UProcGenPriorsConfig>()->ToPriors();
}

FGalaxyGenerator UUniverseSubsystem::MakeGalaxy() const
{
    return FGalaxyGenerator(RootSeed, GetPriors());
}

TOptional<FStarSystem> UUniverseSubsystem::GetSystemAt(const FUniversePosition& Where) const
{
    const FGalaxyGenerator Galaxy = MakeGalaxy();
    if (const TOptional<FStarSystemStub> Stub = Galaxy.FindSystemAt(Where, FStarSystem::InSystemRadiusCm))
    {
        return Galaxy.GenerateSystem(*Stub);
    }
    return {};
}

TOptional<FStarSystem> UUniverseSubsystem::GetSystem(const FSystemId& Id) const
{
    const FGalaxyGenerator Galaxy = MakeGalaxy();
    if (const TOptional<FStarSystemStub> Stub = Galaxy.GenerateStub(Id))
    {
        return Galaxy.GenerateSystem(*Stub);
    }
    return {};
}

TArray<FStarSystemStub> UUniverseSubsystem::GetSystemsNear(const FUniversePosition& Where, double RadiusCm) const
{
    return MakeGalaxy().FindSystemsWithin(Where, RadiusCm);
}

FSystemId UUniverseSubsystem::GetStartSystem() const
{
    return MakeGalaxy().StartSystem();
}

uint64 UUniverseSubsystem::ResolveSeed(int64 ConfigSeed, const TCHAR* CommandLine)
{
    FString Text;
    if (CommandLine && FParse::Value(CommandLine, TEXT("UniverseSeed="), Text))
    {
        uint64 Seed = 0;
        if (ParseSeed(Text, Seed))
        {
            return Seed;
        }
        UE_LOG(LogUniverse, Warning, TEXT("-UniverseSeed=%s is not a seed; using the configured one"), *Text);
    }
    return static_cast<uint64>(ConfigSeed);
}
