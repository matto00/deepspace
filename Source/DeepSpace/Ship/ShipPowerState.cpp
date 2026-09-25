#include "Ship/ShipPowerState.h"

namespace ShipPower
{
    const FName Lights(TEXT("Power.Lights"));
    const FName Boosters(TEXT("Power.Boosters"));
    const FName Engine(TEXT("Power.Engine"));
}

void FShipPowerState::SetReactorOutput(float Watts)
{
    ReactorOutput = Watts;
    Allocate();
}

float FShipPowerState::GetReactorOutput() const
{
    return ReactorOutput;
}

bool FShipPowerState::AddDraw(FName ModuleId, float Watts)
{
    if (Draws.Contains(ModuleId))
    {
        return false;
    }
    Draws.Add(ModuleId, Watts);
    Allocate();
    return true;
}

bool FShipPowerState::RemoveDraw(FName ModuleId)
{
    if (Draws.Remove(ModuleId) == 0)
    {
        return false;
    }
    Allocate();
    return true;
}

float FShipPowerState::GetTotalDraw() const
{
    float Total = GetAllocatedPower();
    for (const TPair<FName, float>& Draw : Draws)
    {
        Total += Draw.Value;
    }
    return Total;
}

float FShipPowerState::GetHeadroom() const
{
    return ReactorOutput - GetTotalDraw();
}

bool FShipPowerState::IsOverloaded() const
{
    return GetHeadroom() < 0.0f;
}

void FShipPowerState::SetConsumer(FName ConsumerId, float Want, float Weight)
{
    FConsumer& Consumer = Consumers.FindOrAdd(ConsumerId);
    Consumer.Want = FMath::Max(0.0f, Want);
    Consumer.Weight = Weight;
    Allocate();
}

void FShipPowerState::SetWant(FName ConsumerId, float Want)
{
    Consumers.FindOrAdd(ConsumerId).Want = FMath::Max(0.0f, Want);
    Allocate();
}

float FShipPowerState::GetWant(FName ConsumerId) const
{
    const FConsumer* Consumer = Consumers.Find(ConsumerId);
    return Consumer ? Consumer->Want : 0.0f;
}

void FShipPowerState::SetWeight(FName ConsumerId, float Weight)
{
    Consumers.FindOrAdd(ConsumerId).Weight = Weight;
    Allocate();
}

float FShipPowerState::GetWeight(FName ConsumerId) const
{
    const FConsumer* Consumer = Consumers.Find(ConsumerId);
    return Consumer ? Consumer->Weight : 0.0f;
}

bool FShipPowerState::RemoveConsumer(FName ConsumerId)
{
    if (Consumers.Remove(ConsumerId) == 0)
    {
        return false;
    }
    Allocate();
    return true;
}

float FShipPowerState::GetShare(FName ConsumerId) const
{
    const FConsumer* Consumer = Consumers.Find(ConsumerId);
    return Consumer ? Consumer->Share : 0.0f;
}

float FShipPowerState::GetSatisfaction(FName ConsumerId) const
{
    const FConsumer* Consumer = Consumers.Find(ConsumerId);
    if (!Consumer || Consumer->Want <= 0.0f)
    {
        return 1.0f;
    }
    return FMath::Clamp(Consumer->Share / Consumer->Want, 0.0f, 1.0f);
}

float FShipPowerState::GetAllocatedPower() const
{
    float Total = 0.0f;
    for (const TPair<FName, FConsumer>& Entry : Consumers)
    {
        Total += Entry.Value.Share;
    }
    return Total;
}

float FShipPowerState::GetAvailablePower() const
{
    float Committed = 0.0f;
    for (const TPair<FName, float>& Draw : Draws)
    {
        Committed += Draw.Value;
    }
    return FMath::Max(0.0f, ReactorOutput - Committed);
}

void FShipPowerState::Allocate()
{
    // Who is actually asking. A consumer wanting nothing is switched off and
    // takes no part; a consumer with no weight makes no claim, which is a
    // preference the player is allowed to express and not an error.
    TArray<FName> Short;
    for (TPair<FName, FConsumer>& Entry : Consumers)
    {
        Entry.Value.Share = 0.0f;
        if (Entry.Value.Want > 0.0f && Entry.Value.Weight > 0.0f)
        {
            Short.Add(Entry.Key);
        }
    }

    float Remaining = GetAvailablePower();

    while (Short.Num() > 0 && Remaining > 0.0f)
    {
        float TotalWeight = 0.0f;
        for (const FName& Id : Short)
        {
            TotalWeight += Consumers[Id].Weight;
        }
        if (TotalWeight <= 0.0f)
        {
            break;
        }

        // Cap whoever this pass would over-supply, and go round again with
        // what they did not need. Only if nobody caps is the split final.
        TArray<FName> StillShort;
        float Capped = 0.0f;
        bool bAnyCapped = false;
        for (const FName& Id : Short)
        {
            FConsumer& Consumer = Consumers[Id];
            const float Provisional = Remaining * Consumer.Weight / TotalWeight;
            if (Provisional >= Consumer.Want)
            {
                Consumer.Share = Consumer.Want;
                Capped += Consumer.Want;
                bAnyCapped = true;
            }
            else
            {
                StillShort.Add(Id);
            }
        }

        if (!bAnyCapped)
        {
            for (const FName& Id : Short)
            {
                FConsumer& Consumer = Consumers[Id];
                Consumer.Share = Remaining * Consumer.Weight / TotalWeight;
            }
            return;
        }

        Remaining -= Capped;
        Short = MoveTemp(StillShort);
    }
}

TArray<FName> FShipPowerState::GetConsumers() const
{
    TArray<FName> Ids;
    Consumers.GetKeys(Ids);

    // Sorted, so two screens listing the consumers list them in the same
    // order. A TMap's iteration order is an implementation detail and would
    // otherwise make the laptop and the console disagree about which row is
    // which.
    Ids.Sort(FNameLexicalLess());
    return Ids;
}
