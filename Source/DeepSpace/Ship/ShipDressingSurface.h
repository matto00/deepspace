#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Ship/ShipDressingTypes.h"
#include "ShipDressingSurface.generated.h"

/**
 * Somewhere the dressing may leave things: a table top, a shelf, a bench
 * seat, exported by the layout (lived-in decision 1b).
 *
 * No logic, only what Tools/build_hauler.py assigns from
 * placement.resolve_surfaces -- the use ADR 0002 allows, and the shape
 * AShipHumSource has. Its transform is the centre of the resting plane,
 * turned as its prop faces (90 degree steps); everything else is in the
 * surface's own frame. UShipDressingSubsystem finds it by the tag
 * Dress.Surface, which the level script applies, and never by name.
 *
 * Tools/verify_level.py checks every one against the layout, so the C++
 * reads only what the verifier has passed. When the layout is generated in
 * C++ these markers, their check and resolve_surfaces are deleted, and the
 * generator hands ShipDressing::Dress its surfaces directly.
 */
UCLASS()
class DEEPSPACE_API AShipDressingSurface : public AActor
{
    GENERATED_BODY()

public:
    AShipDressingSurface();

    /** Selects the room stream. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    FName Room;

    /** "<prop>.<surface>", such as counter.top or wall_rack.shelf_1. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    FName Kind;

    /** The placement's n, as in its prop_<name>_<n> labels. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    int32 Ordinal = 0;

    /** Extent in the surface's frame: X across, Y along. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    FVector2D Size = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    EDressEdge Back = EDressEdge::NegX;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    EDressUse Use = EDressUse::Centre;

    /** The tallest thing that fits, piles included. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    float Clear = 40.0f;

    /** Surface-local rectangles nothing may cover: the laptop, a lamp, the
     *  chart, the workbench's screen. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dressing")
    TArray<FBox2D> Excludes;
};
