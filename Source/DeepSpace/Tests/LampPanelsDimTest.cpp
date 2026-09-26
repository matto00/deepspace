#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Ship/ShipLightingSubsystem.h"
#include "Ship/ShipPowerState.h"
#include "Ship/ShipSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLampPanelsDimTest,
    "DeepSpace.Ship.LampPanelsDim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** A lamp panel as Tools/build_hauler.py leaves one: a Static box
     *  wearing its room's lamp material, tagged or not. */
    AStaticMeshActor* SpawnPanel(UWorld* World, UStaticMesh* Cube, UMaterialInterface* Material, bool bTagged)
    {
        AStaticMeshActor* Panel = World->SpawnActor<AStaticMeshActor>();
        UStaticMeshComponent* Mesh = Panel->GetStaticMeshComponent();
        Mesh->SetStaticMesh(Cube);
        Mesh->SetMaterial(0, Material);
        if (bTagged)
        {
            Panel->Tags.Add(ShipLighting::LampsTag);
        }
        return Panel;
    }

    /** What the panel glows with right now: its material's Colour. */
    FLinearColor Glow(const AStaticMeshActor* Panel)
    {
        FLinearColor Colour = FLinearColor::Black;
        if (const UMaterialInterface* Material = Panel->GetStaticMeshComponent()->GetMaterial(0))
        {
            Material->GetVectorParameterValue(FHashedMaterialParameterInfo(ShipLighting::LampColourParameter), Colour);
        }
        return Colour;
    }

    bool SameColour(const FLinearColor& A, const FLinearColor& B, float Tolerance)
    {
        return FMath::Abs(A.R - B.R) <= Tolerance * FMath::Max(1.0f, FMath::Abs(B.R))
            && FMath::Abs(A.G - B.G) <= Tolerance * FMath::Max(1.0f, FMath::Abs(B.G))
            && FMath::Abs(A.B - B.B) <= Tolerance * FMath::Max(1.0f, FMath::Abs(B.B));
    }
}

/**
 * A starved room's panels dim and brown out with its lights, found by the
 * `Power.Lamps` tag and nothing else (lived-in decision 10).
 *
 * With the real lamp materials the level build makes. The panels take the
 * same relative brown-out as the lights, each on its own rated colour: a
 * 2700 K bunk panel starved must go the way the 2700 K light under it goes,
 * which a fixed amber target -- bluer than the bunk already is -- would not.
 */
bool FLampPanelsDimTest::RunTest(const FString& Parameters)
{
    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UMaterialInterface* Bunk = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_Ship_lamp_bunk.MI_Ship_lamp_bunk"));
    UMaterialInterface* Airlock = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/MI_Ship_lamp_airlock.MI_Ship_lamp_airlock"));
    if (!TestNotNull(TEXT("the cube"), Cube)
        || !TestNotNull(TEXT("the bunk's lamp material, from build_hauler.py"), Bunk)
        || !TestNotNull(TEXT("the airlock's"), Airlock))
    {
        return false;
    }
    FLinearColor BunkRated, AirlockRated;
    if (!TestTrue(TEXT("a lamp material glows with a Colour parameter"),
                  Bunk->GetVectorParameterValue(FHashedMaterialParameterInfo(ShipLighting::LampColourParameter), BunkRated)
                  && Airlock->GetVectorParameterValue(FHashedMaterialParameterInfo(ShipLighting::LampColourParameter), AirlockRated)))
    {
        return false;
    }

    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("LampPanelsDimTestWorld"));
    FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
    Context.SetCurrentWorld(World);

    AStaticMeshActor* BunkPanel = SpawnPanel(World, Cube, Bunk, true);
    AStaticMeshActor* AirlockPanel = SpawnPanel(World, Cube, Airlock, true);
    AStaticMeshActor* Untagged = SpawnPanel(World, Cube, Bunk, false);

    // The light the bunk panel belongs to: the bunk's 2700 K, as sRGB, the
    // way build_hauler.py writes a light's colour.
    APointLight* BunkLight = World->SpawnActor<APointLight>();
    BunkLight->Tags.Add(ShipPower::Lights);
    UPointLightComponent* Bulb = BunkLight->FindComponentByClass<UPointLightComponent>();
    Bulb->SetMobility(EComponentMobility::Movable);
    Bulb->SetLightColor(FLinearColor(FColor(255, 167, 87)));

    // Play begins as the level's does: the subsystem finds everything then.
    World->InitializeActorsForPlay(FURL());
    World->BeginPlay();
    World->GetWorldSettings()->NotifyBeginPlay();

    UShipSubsystem* Ship = World->GetSubsystem<UShipSubsystem>();
    UShipLightingSubsystem* Lighting = World->GetSubsystem<UShipLightingSubsystem>();
    if (!TestNotNull(TEXT("the world has a ship"), Ship) || !TestNotNull(TEXT("and a lighting subsystem"), Lighting))
    {
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return false;
    }

    TestEqual(TEXT("begin-play finds the two tagged panels, and not the untagged one"), Lighting->GetLampCount(), 2);

    Lighting->Tick(0.016f);
    TestTrue(TEXT("fed, the bunk panel glows at its rating"), SameColour(Glow(BunkPanel), BunkRated, 0.01f));
    TestTrue(TEXT("and the airlock's at its own"), SameColour(Glow(AirlockPanel), AirlockRated, 0.01f));
    const FLinearColor LightFed = Bulb->GetLightColor();

    // Starved: the lights' weight to nothing, so they brown out fully.
    Ship->SetConsumerWeight(ShipPower::Lights, 0.0f);
    Lighting->Tick(0.016f);
    const FLinearColor LightStarved = Bulb->GetLightColor();
    for (const TPair<const AStaticMeshActor*, FLinearColor>& Panel :
         { TPair<const AStaticMeshActor*, FLinearColor>(BunkPanel, BunkRated),
           TPair<const AStaticMeshActor*, FLinearColor>(AirlockPanel, AirlockRated) })
    {
        const FLinearColor Now = Glow(Panel.Key);
        const FLinearColor Rated = Panel.Value;
        const FString Which = Panel.Key == BunkPanel ? TEXT("bunk") : TEXT("airlock");
        TestTrue(FString::Printf(TEXT("%s: starved, the panel dims (red %.3f of %.3f)"), *Which, Now.R, Rated.R),
                 Now.R > 0.0f && Now.R < 0.2f * Rated.R);
        // The panel's colour moves as the light's does: the same brown-out,
        // relative to each one's own rating. To 5%, because a light keeps its
        // colour as 8-bit sRGB and a starved blue channel is a few dozen
        // counts; a fixed amber target misses the bunk by 60% and more.
        const float PanelGreen = (Now.G / Now.R) / (Rated.G / Rated.R);
        const float LightGreen = (LightStarved.G / LightStarved.R) / (LightFed.G / LightFed.R);
        const float PanelBlue = (Now.B / Now.R) / (Rated.B / Rated.R);
        const float LightBlue = (LightStarved.B / LightStarved.R) / (LightFed.B / LightFed.R);
        TestTrue(FString::Printf(TEXT("%s: its green falls against red as the light's does (%.3f, the light %.3f)"),
                                 *Which, PanelGreen, LightGreen),
                 FMath::Abs(PanelGreen / LightGreen - 1.0f) <= 0.05f);
        TestTrue(FString::Printf(TEXT("%s: and its blue (%.3f, the light %.3f)"), *Which, PanelBlue, LightBlue),
                 FMath::Abs(PanelBlue / LightBlue - 1.0f) <= 0.05f);
    }
    {
        const FLinearColor Now = Glow(BunkPanel);
        TestTrue(TEXT("the warm bunk panel goes deeper amber, never whiter"),
                 Now.B / Now.R < BunkRated.B / BunkRated.R && Now.G / Now.R < BunkRated.G / BunkRated.R);
    }
    TestTrue(TEXT("the untagged panel is not touched: the tag is the contract"),
             Untagged->GetStaticMeshComponent()->GetMaterial(0) == Bunk);

    // Found again while starved, the rating is still the level's, not the
    // dimmed glow this subsystem wrote into the panel.
    Lighting->Refresh();
    TestEqual(TEXT("a second Refresh finds the same two panels"), Lighting->GetLampCount(), 2);
    Ship->SetConsumerWeight(ShipPower::Lights, 1.0f);
    Lighting->Tick(0.016f);
    TestTrue(TEXT("fed again, it is back at its rating"), SameColour(Glow(BunkPanel), BunkRated, 0.01f));

    // Switched off at the console, the panels go dark with the lights.
    Ship->SetLightsOn(false);
    Lighting->Tick(0.016f);
    const FLinearColor Off = Glow(BunkPanel);
    TestTrue(TEXT("lights off, the panel is dark"), Off.R == 0.0f && Off.G == 0.0f && Off.B == 0.0f);
    Ship->SetLightsOn(true);

    GEngine->DestroyWorldContext(World);
    World->DestroyWorld(false);
    return true;
}

#endif
