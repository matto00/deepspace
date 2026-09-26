#include "Ship/ShipDressingKeepOut.h"

#include "Components/SceneComponent.h"

AShipDressingKeepOut::AShipDressingKeepOut()
{
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

FBox AShipDressingKeepOut::GetBox() const
{
    return FBox::BuildAABB(GetActorLocation(), Size * 0.5);
}
