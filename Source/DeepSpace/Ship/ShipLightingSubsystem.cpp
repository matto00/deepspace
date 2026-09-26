#include "Ship/ShipLightingSubsystem.h"

#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

const FName ShipLighting::LampsTag(TEXT("Power.Lamps"));
const FName ShipLighting::LampColourParameter(TEXT("Colour"));

namespace
{
    // A filament starved of power runs cooler, so a lamp browning out slides
    // down its *own* colour temperature: red holds, green falls, blue falls
    // furthest. It has to be relative to the rating, because the ship's lamps
    // are each their room's kelvin, from a 2700 K bunk to a 6200 K airlock.
    // A fixed amber target is whiter and bluer than the bunk lamp already is,
    // and lerping to it made the warm rooms go cold as they starved. A white
    // lamp still lands on (1, 0.62, 0.30), the amber the ship always browned
    // out to. The lights and their panels take this same function, each on
    // its own rating, so a panel never browns out to a different colour from
    // the light beneath it.
    constexpr float StarvedGreen = 0.62f;
    constexpr float StarvedBlue = 0.30f;

    FLinearColor BrownOut(const FLinearColor& Rated, float Depth)
    {
        return FLinearColor(Rated.R,
                            Rated.G * FMath::Lerp(1.0f, StarvedGreen, Depth),
                            Rated.B * FMath::Lerp(1.0f, StarvedBlue, Depth),
                            Rated.A);
    }
}

void UShipLightingSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    Refresh();
}

void UShipLightingSubsystem::Refresh()
{
    // A light found before keeps the rating it was found with. What it holds
    // now is whatever this subsystem last dimmed it to, and re-reading that
    // while starved would make the brown-out the new rating.
    TMap<const UPointLightComponent*, FShipLight> Known;
    for (const FShipLight& Entry : Lights)
    {
        if (const UPointLightComponent* Light = Entry.Light.Get())
        {
            Known.Add(Light, Entry);
        }
    }
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

        if (const FShipLight* Previous = Known.Find(Light))
        {
            Lights.Add(*Previous);
            continue;
        }
        FShipLight Entry;
        Entry.Light = Light;
        Entry.RatedIntensity = Light->Intensity;
        Entry.RatedColour = Light->GetLightColor();
        Lights.Add(Entry);
    }

    Lamps.Reset();
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (!It->ActorHasTag(ShipLighting::LampsTag))
        {
            continue;
        }
        TInlineComponentArray<UMeshComponent*> Meshes(*It);
        for (UMeshComponent* Mesh : Meshes)
        {
            for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
            {
                UMaterialInterface* Material = Mesh->GetMaterial(Slot);
                if (!Material)
                {
                    continue;
                }
                // Refreshed twice, a panel already wears the instance this
                // subsystem made and dimmed; its rating is its parent's.
                const UMaterialInstanceDynamic* Existing = Cast<UMaterialInstanceDynamic>(Material);
                const UMaterialInterface* Rated = Existing && Existing->Parent ? Existing->Parent.Get() : Material;

                FShipLamp Entry;
                if (!Rated->GetVectorParameterValue(FHashedMaterialParameterInfo(ShipLighting::LampColourParameter),
                                                    Entry.RatedColour))
                {
                    continue;
                }
                Entry.Material = Mesh->CreateDynamicMaterialInstance(Slot);
                if (Entry.Material.IsValid())
                {
                    Lamps.Add(Entry);
                }
            }
        }
    }
}

int32 UShipLightingSubsystem::GetLightCount() const
{
    return Lights.Num();
}

int32 UShipLightingSubsystem::GetLampCount() const
{
    return Lamps.Num();
}

void UShipLightingSubsystem::Tick(float DeltaTime)
{
    const UShipSubsystem* Ship = UShipSubsystem::Get(this);
    if (!Ship || (Lights.Num() == 0 && Lamps.Num() == 0))
    {
        return;
    }

    Phase += DeltaTime;

    // Asked for fresh, every frame. Nothing here keeps a copy of the
    // allocation: the subsystem is authoritative and this is a view of it.
    const bool bOn = Ship->AreLightsOn();
    const float Feed = Ship->GetConsumerSatisfaction(ShipPower::Lights);

    // How far into the brown-out we are, 0 at the threshold and 1 when
    // starved. The warmth and the flicker both follow it, so the light gets
    // visibly *unwell* rather than merely dimmer -- which is what stops
    // dimming reading as a rendering bug.
    const float Depth = 1.0f - FMath::Clamp(Feed / BrownOutBelow, 0.0f, 1.0f);

    float Scale = 0.0f;
    if (bOn)
    {
        Scale = FMath::Max(Feed, StarvedGlow);

        if (Feed < BrownOutBelow)
        {
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
            Light->SetLightColor(BrownOut(Entry.RatedColour, Depth));
        }
    }

    // A panel is the light it belongs to, seen: the same scale, flicker
    // included, and the same brown-out on its own rated colour. Switched
    // off, it is dark, as the light is.
    for (const FShipLamp& Entry : Lamps)
    {
        UMaterialInstanceDynamic* Material = Entry.Material.Get();
        if (!Material)
        {
            continue;
        }
        FLinearColor Glow = BrownOut(Entry.RatedColour, Depth) * Scale;
        Glow.A = Entry.RatedColour.A;
        Material->SetVectorParameterValue(ShipLighting::LampColourParameter, Glow);
    }
}

TStatId UShipLightingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UShipLightingSubsystem, STATGROUP_Tickables);
}
