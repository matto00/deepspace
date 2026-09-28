#include "Ship/ShipModuleDataAsset.h"

FShipPartSpec UShipModuleDataAsset::GetSpec() const
{
    FShipPartSpec Spec;
    Spec.Id = ModuleId;
    Spec.Bay = Bay;
    Spec.Draw = PowerDraw;
    Spec.Ratings = Ratings;
    return Spec;
}
