#include "Ship/ShipLightingSubsystem.h"

#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

namespace
{
    // A fixture browning out goes amber, because a filament starved of power
    // runs cooler. The ship's lamps are a neutral-cool white when fed.
    const FLinearColor BrownOutColour(1.0f, 0.62f, 0.30f, 1.0f);
}

void UShipLightingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    Refresh();
}

void UShipLightingSubsystem::Refresh()
{
    Lights.Reset();

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->ActorHasTag(ShipPower::Lights))
        {
            continue;
        }
        UPointLightComponent* Light = It->FindComponentByClass<UPointLightComponent>();
        if (!Light)
        {
            continue;
        }

        FShipLight Entry;
        Entry.Light = Light;
        Entry.RatedIntensity = Light->Intensity;
        Entry.RatedColour = Light->GetLightColor();
        Lights.Add(Entry);
    }
}

int32 UShipLightingSubsystem::GetLightCount() const
{
    return Lights.Num();
}

void UShipLightingSubsystem::Tick(float DeltaTime)
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship || Lights.Num() == 0)
    {
        return;
    }

    Phase += DeltaTime;

    // Asked for fresh, every frame. Nothing here keeps a copy of the
    // allocation: the subsystem is authoritative and this is a view of it.
    const bool bOn = Ship->AreLightsOn();
    const float Feed = Ship->GetConsumerSatisfaction(ShipPower::Lights);

    float Scale = 0.0f;
    FLinearColor Colour = FLinearColor::White;
    if (bOn)
    {
        Scale = FMath::Max(Feed, StarvedGlow);

        if (Feed < BrownOutBelow)
        {
            // How far into the brown-out we are, 0 at the threshold and 1
            // when starved. Both the warmth and the flicker follow it, so
            // the light gets visibly *unwell* rather than merely dimmer --
            // which is what stops dimming reading as a rendering bug.
            const float Depth = 1.0f - FMath::Clamp(Feed / BrownOutBelow, 0.0f, 1.0f);

            // Two incommensurate rates, so the wobble never settles into a
            // pulse the eye can predict. Deterministic, not random: the same
            // allocation always looks the same way.
            const float Wobble = FMath::Sin(Phase * 37.0f) * FMath::Sin(Phase * 13.7f);
            Scale *= 1.0f + 0.22f * Depth * Wobble;
        }
    }

    for (const FShipLight& Entry : Lights)
    {
        UPointLightComponent* Light = Entry.Light.Get();
        if (!Light)
        {
            continue;
        }
        Light->SetIntensity(Entry.RatedIntensity * Scale);
        if (bOn)
        {
            const float Depth = 1.0f - FMath::Clamp(Feed / BrownOutBelow, 0.0f, 1.0f);
            Light->SetLightColor(FMath::Lerp(Entry.RatedColour, BrownOutColour, Depth));
        }
    }
}

TStatId UShipLightingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UShipLightingSubsystem, STATGROUP_Tickables);
}
