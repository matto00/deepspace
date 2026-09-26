#include "Ship/ShipDressingSurface.h"

#include "Components/SceneComponent.h"

AShipDressingSurface::AShipDressingSurface()
{
    // Only somewhere to stand: the root carries the transform and nothing
    // hangs off it, so there is no child to strand at the origin.
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}
