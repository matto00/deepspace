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

    // An even split to start with, which is a starting point and not a
    // recommendation: every split is viable and none is correct.
    PowerState.SetConsumer(ShipPower::Lights, LightsWant, 1.0f);
    PowerState.SetConsumer(ShipPower::Boosters, BoostersWant, 1.0f);
    PowerState.SetConsumer(ShipPower::Engine, EngineWant, 1.0f);
}

TArray<FName> UShipSubsystem::GetPowerConsumers() const
{
    return PowerState.GetConsumers();
}

float UShipSubsystem::GetConsumerWeight(FName ConsumerId) const
{
    return PowerState.GetWeight(ConsumerId);
}

void UShipSubsystem::SetConsumerWeight(FName ConsumerId, float Weight)
{
    PowerState.SetWeight(ConsumerId, Weight);
}

float UShipSubsystem::GetConsumerShare(FName ConsumerId) const
{
    return PowerState.GetShare(ConsumerId);
}

float UShipSubsystem::GetConsumerWant(FName ConsumerId) const
{
    return PowerState.GetWant(ConsumerId);
}

float UShipSubsystem::GetConsumerSatisfaction(FName ConsumerId) const
{
    return PowerState.GetSatisfaction(ConsumerId);
}

bool UShipSubsystem::AreLightsOn() const
{
    return bLightsOn;
}

void UShipSubsystem::SetLightsOn(bool bOn)
{
    bLightsOn = bOn;

    // Want, not weight: the player's weight for the lights is a preference
    // and survives them being switched off and back on.
    PowerState.SetWant(ShipPower::Lights, bOn ? LightsWant : 0.0f);
}

float UShipSubsystem::GetJumpCharge() const
{
    return static_cast<float>(FlightState.GetJumpCharge());
}

float UShipSubsystem::GetLinearAcceleration() const
{
    return static_cast<float>(FlightState.GetLimits().LinearAcceleration);
}

void UShipSubsystem::ApplyAllocation(float DeltaSeconds)
{
    // Asked for fresh every frame and never stored. A cached satisfaction is
    // how two things that read the same allocation start disagreeing.
    const float BoosterFeed = PowerState.GetSatisfaction(ShipPower::Boosters);
    const float EngineFeed = PowerState.GetSatisfaction(ShipPower::Engine);

    FShipFlightLimits Limits = FlightState.GetLimits();
    const FShipFlightLimits Rated = FShipFlightLimits::Cruise();
    Limits.LinearAcceleration = Rated.LinearAcceleration
        * (StarvedBoosterThrust + (1.0f - StarvedBoosterThrust) * BoosterFeed);
    FlightState.SetLimits(Limits);

    FlightState.ChargeJumpDrive(DeltaSeconds, EngineFeed);
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
    ApplyAllocation(DeltaTime);
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
