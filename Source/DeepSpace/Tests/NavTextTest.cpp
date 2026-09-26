#include "Misc/AutomationTest.h"
#include "UI/NavText.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FNavTextTest,
    "DeepSpace.UI.NavText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    bool HasNumber(const FString& Text)
    {
        for (const TCHAR Character : Text)
        {
            if (FChar::IsDigit(Character) || Character == TEXT('%'))
            {
                return true;
            }
        }
        return false;
    }

    FVector FromAngles(double AcrossDeg, double UpDeg)
    {
        const double Across = FMath::DegreesToRadians(AcrossDeg);
        const double Up = FMath::DegreesToRadians(UpDeg);
        return FVector(FMath::Cos(Up) * FMath::Cos(Across), FMath::Cos(Up) * FMath::Sin(Across), FMath::Sin(Up));
    }

    const EJumpState AllStates[] = { EJumpState::Idle, EJumpState::Winding, EJumpState::Ready, EJumpState::Transit };
}

bool FNavTextTest::RunTest(const FString& Parameters)
{
    const double Cone = FNavTuning().ConeRadians;
    const FString Degree = TEXT("°");

    // Inside the cone the degrees disappear: aligned is aligned, and a
    // number there would be a target to chase.
    {
        TestEqual(TEXT("the nose is dead ahead"), NavText::Bearing(FVector::ForwardVector, Cone), FString(TEXT("dead ahead")));
        const FString Inside = NavText::Bearing(FromAngles(-5.0, 4.0), Cone);
        TestEqual(TEXT("inside the cone is dead ahead"), Inside, FString(TEXT("dead ahead")));
        TestFalse(TEXT("with no digits"), HasNumber(Inside));
    }

    // The four sides, and the example the HUD is designed around.
    {
        TestEqual(TEXT("-Y is port"), NavText::Bearing(FVector(0.0, -1.0, 0.0), Cone), TEXT("90") + Degree + TEXT(" to port"));
        TestEqual(TEXT("+Y is starboard"), NavText::Bearing(FVector(0.0, 1.0, 0.0), Cone), TEXT("90") + Degree + TEXT(" to starboard"));
        TestEqual(TEXT("+Z is up"), NavText::Bearing(FVector(0.0, 0.0, 1.0), Cone), TEXT("90") + Degree + TEXT(" up"));
        TestEqual(TEXT("-Z is down"), NavText::Bearing(FVector(0.0, 0.0, -1.0), Cone), TEXT("90") + Degree + TEXT(" down"));
        TestEqual(TEXT("12 to port, 3 up"), NavText::Bearing(FromAngles(-12.0, 3.0), Cone),
                  TEXT("12") + Degree + TEXT(" to port, 3") + Degree + TEXT(" up"));
        TestEqual(TEXT("a level bearing names no height"), NavText::Bearing(FromAngles(30.0, 0.0), Cone),
                  TEXT("30") + Degree + TEXT(" to starboard"));
        TestEqual(TEXT("it is not normalised by the caller"), NavText::Bearing(FVector(0.0, 0.0, -250.0), Cone),
                  TEXT("90") + Degree + TEXT(" down"));
    }

    // Past 90 degrees, degrees stop helping: astern, and which way to turn.
    {
        TestEqual(TEXT("dead astern"), NavText::Bearing(-FVector::ForwardVector, Cone), FString(TEXT("astern")));
        TestEqual(TEXT("astern to port"), NavText::Bearing(FromAngles(-150.0, 0.0), Cone), FString(TEXT("astern, to port")));
        TestEqual(TEXT("astern to starboard and down"), NavText::Bearing(FromAngles(120.0, -20.0), Cone),
                  FString(TEXT("astern, to starboard, down")));
        TestFalse(TEXT("astern carries no number"), HasNumber(NavText::Bearing(FromAngles(170.0, 10.0), Cone)));
    }

    // The jump is a word, never a number, in every state and on every
    // surface: no percentage, no countdown, no ETA.
    for (const EJumpState State : AllStates)
    {
        TestFalse(TEXT("the HUD word has no number"), HasNumber(NavText::Jump(State)));
        TestFalse(TEXT("the chart word has no number"), HasNumber(NavText::JumpWord(State)));
        TestFalse(TEXT("every state has a word"), NavText::JumpWord(State).IsEmpty());
    }
    TestEqual(TEXT("winding"), NavText::Jump(EJumpState::Winding), FString(TEXT("JUMP WINDING")));
    TestEqual(TEXT("ready"), NavText::Jump(EJumpState::Ready), FString(TEXT("JUMP READY")));
    TestEqual(TEXT("the chart says between stars"), NavText::JumpWord(EJumpState::Transit), FString(TEXT("Between stars")));

    // The HUD's line with a course: the jump, the name, the bearing.
    {
        TestEqual(TEXT("the designed line"),
                  NavText::Jump(EJumpState::Ready, TEXT("Kessa"), FromAngles(-12.0, 0.0), Cone),
                  TEXT("JUMP READY · Kessa · 12") + Degree + TEXT(" to port"));
        TestEqual(TEXT("aligned, it says so"),
                  NavText::Jump(EJumpState::Winding, TEXT("Kessa"), FVector::ForwardVector, Cone),
                  FString(TEXT("JUMP WINDING · Kessa · dead ahead")));
        TestEqual(TEXT("between stars there is nothing to steer"),
                  NavText::Jump(EJumpState::Transit, TEXT("Kessa"), FromAngles(40.0, 0.0), Cone),
                  FString(TEXT("BETWEEN STARS · Kessa")));
    }

    // Every class has its own colour word, and none is a letter or a number.
    {
        TSet<FString> Words;
        for (int32 Index = 0; Index < NumStarClasses; ++Index)
        {
            const FString Word = NavText::StarClass(static_cast<EStarClass>(Index));
            TestFalse(TEXT("a class word has no number"), HasNumber(Word));
            TestTrue(TEXT("a class word is a word"), Word.Len() > 1);
            Words.Add(Word);
        }
        TestEqual(TEXT("each class is worded differently"), Words.Num(), NumStarClasses);
        TestEqual(TEXT("an M star is a red dwarf"), NavText::StarClass(EStarClass::M), FString(TEXT("red dwarf")));
    }

    // The chart's words, pinned here so that the chart, the HUD and
    // ds.Nav.Near, which all ask NavText, say exactly these.
    {
        TestEqual(TEXT("a place is its name and its colour"),
                  NavText::Place(TEXT("Kessa"), EStarClass::M), FString(TEXT("Kessa · red dwarf")));
        TestEqual(TEXT("and the chart adds that you have been"),
                  NavText::Place(TEXT("Kessa"), EStarClass::M, true), FString(TEXT("Kessa · red dwarf · visited")));
        TestEqual(TEXT("and nothing when you have not"),
                  NavText::Place(TEXT("Kessa"), EStarClass::M, false), FString(TEXT("Kessa · red dwarf")));
        TestEqual(TEXT("visited is the word"), NavText::Visited(true), FString(TEXT("visited")));
        TestTrue(TEXT("and somewhere not yet visited says nothing, never 'unvisited'"), NavText::Visited(false).IsEmpty());

        TestEqual(TEXT("a distance is light years to a tenth"),
                  NavText::Distance(4.24 * UniverseUnits::CmPerLightYear), FString(TEXT("4.2 ly")));
        TestEqual(TEXT("and rounds as it reads"),
                  NavText::Distance(11.96 * UniverseUnits::CmPerLightYear), FString(TEXT("12.0 ly")));

        TestEqual(TEXT("no course is None"), NavText::NoCourse(), FString(TEXT("None")));
        TestEqual(TEXT("a course is its name and its bearing, joined as every line is"),
                  NavText::Course(TEXT("Kessa"), FVector::ForwardVector, Cone), FString(TEXT("Kessa · dead ahead")));
        TestEqual(TEXT("in the helm's bearing words"),
                  NavText::Course(TEXT("Kessa"), FromAngles(12.0, 0.0), Cone),
                  FString(TEXT("Kessa · ")) + NavText::Bearing(FromAngles(12.0, 0.0), Cone));
        TestEqual(TEXT("with no bearing, the name alone"),
                  NavText::Course(TEXT("Kessa"), TOptional<FVector>(), Cone), FString(TEXT("Kessa")));
        TestTrue(TEXT("the chart's course is the tail of the HUD's line"),
                 NavText::Jump(EJumpState::Ready, TEXT("Kessa"), FromAngles(12.0, 3.0), Cone)
                     .EndsWith(NavText::Course(TEXT("Kessa"), FromAngles(12.0, 3.0), Cone)));
    }

    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
