#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ship/ShipHumComponent.h"
#include "ShipHumSource.generated.h"

/**
 * A point in the ship the hum comes from: the reactor, or a room's air.
 *
 * Spawned by Tools/build_hauler.py from the layout's HUM_SOURCES, which
 * assigns where it stands and its Kind and nothing else (ADR 0002). Its root
 * is its one UShipHumComponent, so there is no child to leave behind at the
 * origin (CLAUDE.md, the Static-child trap).
 */
UCLASS()
class DEEPSPACE_API AShipHumSource : public AActor
{
    GENERATED_BODY()

public:
    AShipHumSource();

    /** Which voice. The level script's to set; handed to the component
     *  before any component begins play. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hum")
    EShipHumKind Kind = EShipHumKind::Reactor;

    UShipHumComponent* GetHum() const;

    virtual void PostInitializeComponents() override;

private:
    UPROPERTY(VisibleAnywhere, Category = "Hum")
    TObjectPtr<UShipHumComponent> Hum;
};
