#pragma once

#include "CoreMinimal.h"
#include "UI/ShipScreenWidget.h"
#include "PowerAllocationWidget.generated.h"

class UProgressBar;
class USlider;
class UTextBlock;

/**
 * The laptop's screen: one weight control per consumer, and how each one is
 * getting on.
 *
 * What is deliberately absent is as important as what is here. No total, no
 * efficiency, no percentage of potential, no suggested split and nothing
 * anywhere that says the ship could be doing better. Every split is viable
 * and none is correct; the screen shows what the player has chosen and what
 * it means, and never invents an optimum to converge on (docs/vision.md).
 *
 * It is also not a station. Nothing drifts while the player is elsewhere, so
 * there is never a reason to come back except wanting to.
 */
UCLASS()
class DEEPSPACE_API UPowerAllocationWidget : public UShipScreenWidget
{
    GENERATED_BODY()

public:
    /** The largest weight a slider can ask for. A ratio, not a budget. */
    static constexpr float MaxWeight = 4.0f;

    /**
     * Rebuilds the rows from the subsystem's consumer list. Public so a test
     * can drive the screen without Slate ever painting.
     */
    void RefreshRows();

    /** Drives the row for this consumer as though its slider had been moved.
     *  The seam the pointer's click ends up at, and what a test drives. */
    void SetRowWeight(FName ConsumerId, float Weight);

    /** What this screen is currently showing for a consumer, so a test can
     *  ask two screens the same question. */
    FText GetRowText(FName ConsumerId) const;

    /** Where this screen's slider currently sits. */
    float GetRowWeight(FName ConsumerId) const;

    /**
     * Pulls every row back into line with the subsystem. Called every frame
     * from NativeTick: the sliders follow the ship, never the other way
     * round, so a weight changed on the console's screen moves this one too.
     */
    void RefreshFromShip();

protected:
    virtual UWidget* BuildScreen() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;

private:
    struct FRow
    {
        FName ConsumerId;
        TObjectPtr<UTextBlock> Label;
        TObjectPtr<USlider> Weight;
        TObjectPtr<UProgressBar> Feed;
    };

    UFUNCTION()
    void HandleWeightChanged(float Value);

    /** Human wording for a consumer id. Ids are contracts with the level;
     *  what the player reads is not. */
    static FText NameOf(FName ConsumerId);

    FText DescribeRow(FName ConsumerId) const;

    UPROPERTY()
    TObjectPtr<class UVerticalBox> Rows;

    TArray<FRow> Consumers;
};
