#include "Misc/AutomationTest.h"
#include "Sky/SkyProjection.h"
#include "Surface/SunShadow.h"
#include "Tests/SkyTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunLightIsTheSkysTest, "DeepSpace.Sky.SunLightIsTheSkys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The cast shadow is cast by the light the sky shades with (the cast-shadow
 * plan's constraint): SunLightOf's direction is Project's LightDirection bit
 * for bit -- the body's centre toward its star, universe axes -- and its
 * radius the star's, asin(R_star / d). Worked out here from the fixture's
 * positions too, so a mutant moving both at once is still caught.
 */
bool FSunLightIsTheSkysTest::RunTest(const FString& Parameters)
{
    const FSkySystem System = SkyTestFixtures::System();
    const FSkyFrame Frame = SkyProjection::Project(System, SkyTestFixtures::Opening(), FSkyViewParams());
    const FSkyBody& Star = System.Bodies[SkyTestFixtures::StarIndex];
    int32 Lit = 0;
    for (int32 Index = 0; Index < System.Bodies.Num(); ++Index)
    {
        const FSkyBody& Body = System.Bodies[Index];
        const SunShadow::FSunLight Sun = SkyProjection::SunLightOf(System, Index);
        if (Body.Kind == ESkyBodyKind::Star)
        {
            TestFalse(TEXT("the star casts on nothing of its own"), Sun.IsSet());
            continue;
        }
        ++Lit;
        const FVector3d Want = FVector3d(Star.Position - Body.Position).GetSafeNormal();
        TestTrue(FString::Printf(TEXT("%s: the shadow's light is the sky's, bit for bit"), *Body.Id.ToString()),
            Sun.Direction == FVector3d(Frame.Bodies[Index].LightDirection));
        TestTrue(FString::Printf(TEXT("%s: from the body's centre toward its star"), *Body.Id.ToString()),
            FVector3d::DotProduct(Sun.Direction, Want) > 1.0 - 1e-12);
        const double Distance = FVector3d(Star.Position - Body.Position).Size();
        TestTrue(FString::Printf(TEXT("%s: the star's own radius, asin(R / d) (%.9f)"), *Body.Id.ToString(), Sun.AngularRadius),
            FMath::IsNearlyEqual(Sun.AngularRadius, FMath::Asin(Star.Radius / Distance), 1e-15));
    }
    TestTrue(TEXT("the fixture has lit bodies"), Lit > 0);
    TestFalse(TEXT("an index the system does not have casts nothing"), SkyProjection::SunLightOf(System, 99).IsSet());
    return true;
}

#endif
