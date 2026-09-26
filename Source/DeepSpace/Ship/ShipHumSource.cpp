#include "Ship/ShipHumSource.h"

AShipHumSource::AShipHumSource()
{
    Hum = CreateDefaultSubobject<UShipHumComponent>(TEXT("Hum"));
    SetRootComponent(Hum);
}

UShipHumComponent* AShipHumSource::GetHum() const
{
    return Hum;
}

void AShipHumSource::PostInitializeComponents()
{
    Super::PostInitializeComponents();

    // After the level's value of Kind is loaded and before BeginPlay, which
    // is where the component starts its voice with whatever kind it has.
    if (Hum)
    {
        Hum->SetKind(Kind);
    }
}
