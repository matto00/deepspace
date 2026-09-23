#include "Ship/ShipSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Ship/ShipModuleDataAsset.h"

UShipSubsystem* UShipSubsystem::Get(const UObject* WorldContext)
{
    if (!WorldContext)
    {
        return nullptr;
    }
    const UWorld* World = WorldContext->GetWorld();
    return World ? World->GetSubsystem<UShipSubsystem>() : nullptr;
}

void UShipSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    PowerState.SetReactorOutput(DefaultReactorOutput);
}

bool UShipSubsystem::InstallModule(UShipModuleDataAsset* Module)
{
    if (!Module)
    {
        return false;
    }
    if (!PowerState.AddDraw(Module->ModuleId, Module->PowerDraw))
    {
        return false;
    }
    InstalledModules.Add(Module);
    return true;
}

bool UShipSubsystem::RemoveModule(UShipModuleDataAsset* Module)
{
    if (!Module)
    {
        return false;
    }
    if (!PowerState.RemoveDraw(Module->ModuleId))
    {
        return false;
    }
    InstalledModules.Remove(Module);
    return true;
}

float UShipSubsystem::GetPowerDraw() const
{
    return PowerState.GetTotalDraw();
}

float UShipSubsystem::GetPowerHeadroom() const
{
    return PowerState.GetHeadroom();
}

float UShipSubsystem::GetReactorOutput() const
{
    return PowerState.GetReactorOutput();
}

bool UShipSubsystem::IsPowerOverloaded() const
{
    return PowerState.IsOverloaded();
}

void UShipSubsystem::SetPilot(APawn* NewPilot)
{
    Pilot = NewPilot;
}

void UShipSubsystem::ClearPilot()
{
    Pilot.Reset();

    // A ship nobody is flying does not keep turning. The throttle stays: a
    // cruise the player set and then walked away from is the point.
    FlightState.ReleaseAttitude();
}

bool UShipSubsystem::IsPiloted() const
{
    return Pilot.IsValid();
}

APawn* UShipSubsystem::GetPilot() const
{
    return Pilot.Get();
}

void UShipSubsystem::Tick(float DeltaTime)
{
    FlightState.Step(DeltaTime);
}

TStatId UShipSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UShipSubsystem, STATGROUP_Tickables);
}

bool UShipSubsystem::SetFlightCommand(APawn* Commander, float Throttle, FVector AttitudeRate)
{
    if (!Commander || Commander != Pilot.Get())
    {
        return false;
    }

    FShipFlightCommand Command;
    Command.Throttle = Throttle;
    Command.AttitudeRate = AttitudeRate;
    FlightState.SetCommand(Command);
    return true;
}

FVector UShipSubsystem::GetShipVelocity() const
{
    return FlightState.GetVelocity();
}

float UShipSubsystem::GetShipSpeed() const
{
    return static_cast<float>(FlightState.GetSpeed());
}

FTransform UShipSubsystem::GetCounterFrameTransform() const
{
    return FlightState.GetCounterFrameTransform();
}

FVector UShipSubsystem::UniverseToWorld(const FUniversePosition& UniversePosition) const
{
    return FlightState.UniverseToWorld(UniversePosition);
}

void UShipSubsystem::PlaceShip(const FUniversePosition& NewPosition, const FQuat& NewOrientation)
{
    FlightState.SetUniverseTransform(NewPosition, NewOrientation);
}

const FShipFlightState& UShipSubsystem::GetFlightState() const
{
    return FlightState;
}
