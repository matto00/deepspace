#include "Ship/ShipPowerState.h"

void FShipPowerState::SetReactorOutput(float Watts)
{
    ReactorOutput = Watts;
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
    return true;
}

bool FShipPowerState::RemoveDraw(FName ModuleId)
{
    return Draws.Remove(ModuleId) > 0;
}

float FShipPowerState::GetTotalDraw() const
{
    float Total = 0.0f;
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
