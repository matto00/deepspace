#include "Universe/UniverseSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDevice.h"
#include "Misc/Parse.h"
#include "Ship/ShipSubsystem.h"
#include "Universe/GalaxyGenerator.h"
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
        const FGenPriors Priors = Universe ? Universe->GetPriors() : FGenPriors{};

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
    Priors = FGenPriors{};

    UE_LOG(LogUniverse, Log, TEXT("Universe seed %llu (0x%016llX)"),
        static_cast<unsigned long long>(RootSeed), static_cast<unsigned long long>(RootSeed));
}

uint64 UUniverseSubsystem::GetRootSeed() const
{
    return RootSeed;
}

FGenPriors UUniverseSubsystem::GetPriors() const
{
    return Priors;
}

FGalaxyGenerator UUniverseSubsystem::MakeGalaxy() const
{
    return FGalaxyGenerator(RootSeed, Priors);
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
