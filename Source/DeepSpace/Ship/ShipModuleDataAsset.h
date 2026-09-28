#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Ship/ShipParts.h"
#include "ShipModuleDataAsset.generated.h"

/**
 * One part: a module in a bay (wear and upgrades decision 1). "Module" and
 * "part" name the same thing. The class keeps its name because renaming a
 * UCLASS strands every asset saved against the old name.
 *
 * Data, never logic (ADR 0002), authored from Tools/ship_parts.json by
 * Tools/setup_ship_parts.py. Never edit one by hand: a number settled into a
 * .uasset is invisible to git, and DeepSpace.Ship.Parts.Contract holds the
 * assets to the JSON.
 */
UCLASS(BlueprintType)
class DEEPSPACE_API UShipModuleDataAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** `<Bay>.<Name>` (Reactor.TwinCore): the catalogue's key, and what the
     *  loadout state saves. Draws are booked by bay, never by this
     *  (decision 4). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FName ModuleId;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FText DisplayName;

    /** Watts off the top while fitted, before any split. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    float PowerDraw = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    TSoftObjectPtr<UStaticMesh> Mesh;

    /** The bay it fits. None until set, and UShipSubsystem::FitPart refuses
     *  a None part rather than defaulting it into the reactor bay. An aux
     *  part says Aux1, and fits either auxiliary slot. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    EShipBay Bay = EShipBay::None;

    /** Its nameplate's words: character and history, never quality
     *  (decision 9). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    FText Words;

    /** The rated values it sets, each one its own bay owns (decision 2). A
     *  rating it lacks reads the stock part's, never zero (decision 3). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Module")
    TMap<EShipRating, double> Ratings;

    /** The part as the pure rules see it (ShipParts::Validate). */
    FShipPartSpec GetSpec() const;
};
