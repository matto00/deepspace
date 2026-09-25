#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ShipConsole.generated.h"

class UInteractableComponent;
class USceneComponent;
class UStaticMeshComponent;
class UWidgetComponent;

/**
 * An engineering console. Reads power figures from UShipSubsystem on demand
 * and never stores them — the discipline that keeps ship state from
 * scattering across actors.
 */
UCLASS()
class DEEPSPACE_API AShipConsole : public AActor
{
    GENERATED_BODY()

public:
    AShipConsole();

    /** Text for the screen. Queries the subsystem fresh on every call. */
    UFUNCTION(BlueprintPure, Category = "Console")
    FText GetReadout() const;

    UWidgetComponent* GetScreen() const { return Screen; }

    /** Implemented in Blueprint to update the screen material. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Console")
    void OnReadoutChanged();

protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;

    /**
     * A bare root so Mesh can be repositioned without moving the actor. The
     * mesh cannot be the root: offsetting a root component *is* moving the
     * actor.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<USceneComponent> Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UInteractableComponent> Interactable;

    /**
     * The console's screen. Configured exactly as any other ship panel, via
     * AShipScreen::ConfigurePanel -- the console is not an AShipScreen, it
     * is a console that carries one.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Console")
    TObjectPtr<UWidgetComponent> Screen;

    /** Panel size, cm and pixels. Sized against the distance a player
     *  actually stands at, not against the editor preview. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console")
    float PanelWidthCm = 58.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console")
    FVector2D DrawSizePixels = FVector2D(600.0f, 400.0f);

    /**
     * How far proud of the panel's *measured* face the screen sits, cm.
     *
     * A clearance, not an absolute offset: the console's depth is whatever
     * mesh and scale the Blueprint assigns, and a hard-coded offset put the
     * screen 1.5 cm inside its own front face -- rendered, and invisible.
     * Meshes do not agree on pivot placement, so the face is measured.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Console")
    float ScreenClearance = 1.5f;

private:
    /**
     * Shifts Mesh so the panel is centred on the actor's origin.
     *
     * Static meshes disagree about where their pivot sits — SM_Cube's is at
     * its minimum corner — so without this the panel hangs off to one side of
     * wherever the console is placed, and the screen text lands on a corner.
     * Measuring the mesh keeps placement predictable whatever is assigned.
     */
    void CentreMesh();

    UFUNCTION()
    void HandleInteracted(AActor* InteractInstigator);

    /** Keeps the reach prompt saying what E will actually do. */
    void SyncPrompt();

    /** Puts the screen just proud of the panel's measured front face. */
    void PlaceScreen();
};
