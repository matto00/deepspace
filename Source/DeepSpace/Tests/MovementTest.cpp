#include "Misc/AutomationTest.h"
#include "Player/MovementRules.h"

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

#endif
