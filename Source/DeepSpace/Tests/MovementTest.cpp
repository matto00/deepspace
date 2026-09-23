#include "Components/CapsuleComponent.h"
#include "Dom/JsonObject.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/DeepSpaceCharacter.h"
#include "Player/MovementRules.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSprintRuleTest,
    "DeepSpace.Player.Sprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSprintRuleTest::RunTest(const FString& Parameters)
{
    // IA_Move: X strafes right, Y moves forward.
    TestTrue(TEXT("straight ahead sprints"), FMovementRules::CanSprint(false, FVector2D(0.0f, 1.0f)));
    TestTrue(TEXT("forward-diagonal sprints"), FMovementRules::CanSprint(false, FVector2D(0.7f, 0.7f)));
    TestFalse(TEXT("pure strafe walks"), FMovementRules::CanSprint(false, FVector2D(1.0f, 0.0f)));
    TestFalse(TEXT("strafe-dominant walks"), FMovementRules::CanSprint(false, FVector2D(0.9f, 0.4f)));
    TestFalse(TEXT("backwards walks"), FMovementRules::CanSprint(false, FVector2D(0.0f, -1.0f)));
    TestFalse(TEXT("standing still walks"), FMovementRules::CanSprint(false, FVector2D::ZeroVector));
    TestFalse(TEXT("a sliver of forward walks"), FMovementRules::CanSprint(false, FVector2D(0.0f, 0.05f)));
    TestFalse(TEXT("crouched never sprints"), FMovementRules::CanSprint(true, FVector2D(0.0f, 1.0f)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMovementContractTest,
    "DeepSpace.Player.MovementContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    /** The character as the game uses it: the Blueprint if it loads, else the C++ class. */
    const ADeepSpaceCharacter* PlayableCharacterDefaults(FString& OutWhich)
    {
        const UClass* Blueprint = LoadClass<ADeepSpaceCharacter>(
            nullptr, TEXT("/Game/Blueprints/BP_DeepSpaceCharacter.BP_DeepSpaceCharacter_C"));
        OutWhich = Blueprint ? TEXT("BP_DeepSpaceCharacter") : TEXT("ADeepSpaceCharacter");
        return Blueprint ? Blueprint->GetDefaultObject<ADeepSpaceCharacter>()
                         : GetDefault<ADeepSpaceCharacter>();
    }
}

bool FMovementContractTest::RunTest(const FString& Parameters)
{
    // The ship's side of the contract, the same file the level validator reads.
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/movement_contract.json"));
    FString Text;
    if (!TestTrue(TEXT("contract file loads: ") + Path, FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Contract;
    if (!TestTrue(TEXT("contract parses as JSON"),
                  FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Contract) && Contract.IsValid()))
    {
        return false;
    }
    const double StandClearance = Contract->GetNumberField(TEXT("stand_clearance"));
    const double CrouchClearance = Contract->GetNumberField(TEXT("crouch_clearance"));
    const double Radius = Contract->GetNumberField(TEXT("capsule_radius"));

    FString Which;
    const ADeepSpaceCharacter* Character = PlayableCharacterDefaults(Which);
    const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
    const double Standing = 2.0 * Capsule->GetUnscaledCapsuleHalfHeight();
    const double Crouched = 2.0 * Character->GetCharacterMovement()->GetCrouchedHalfHeight();

    AddInfo(FString::Printf(TEXT("%s: standing %.0f, crouched %.0f, radius %.0f; ship: stand %.0f, crouch %.0f, radius %.0f"),
        *Which, Standing, Crouched, Capsule->GetUnscaledCapsuleRadius(), StandClearance, CrouchClearance, Radius));

    TestTrue(TEXT("standing capsule fits under the stand clearance"), Standing < StandClearance);
    TestTrue(TEXT("crouched capsule fits under the crouch clearance"), Crouched < CrouchClearance);
    TestTrue(TEXT("crouched capsule is too tall to stand up in a crouch space"), Standing > CrouchClearance);
    TestEqual(TEXT("capsule radius matches the contract"), (double)Capsule->GetUnscaledCapsuleRadius(), Radius);
    return true;
}

#endif
