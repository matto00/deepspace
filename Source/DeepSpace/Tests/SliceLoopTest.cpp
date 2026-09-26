#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipNavState.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyStarfield.h"
#include "Tests/SkyTestWorld.h"
#include "UI/ShipHUDWidget.h"
#include "Universe/StarSystem.h"
#include "Universe/UniverseUnits.h"

#if WITH_DEV_AUTOMATION_TESTS

/*
 * Slice 1's loop with every half in the world at once: the simulation, the
 * sky, the counter-frame and the HUD's words. Each half has its own tests;
 * these are the ones that could not exist until the halves met, and they
 * check what the loop promises the player rather than what any one class
 * does.
 */

namespace SliceLoopTestLocal
{
    using namespace SkyTestWorld;

    /** The index, in FStarSystem::Planets, of the largest planet: the one the
     *  opening shot frames. */
    int32 LargestPlanet(const FStarSystem& System)
    {
        int32 Largest = INDEX_NONE;
        for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
        {
            if (Largest == INDEX_NONE || System.Planets[Index].RadiusEarth > System.Planets[Largest].RadiusEarth)
            {
                Largest = Index;
            }
        }
        return Largest;
    }

    /** The whole degrees NavText put in front of Word ("12° to port"), or 0
     *  when the line does not mention it. */
    double DegreesBefore(const FString& Line, const TCHAR* Word)
    {
        TArray<FString> Parts;
        Line.ParseIntoArray(Parts, TEXT(","));
        for (const FString& Part : Parts)
        {
            if (Part.Contains(Word))
            {
                return FCString::Atod(*Part.TrimStart());
            }
        }
        return 0.0;
    }

    /**
     * What a pilot does with the HUD's line and nothing else: turn toward the
     * side it names, harder the more degrees it gives, and let go at "dead
     * ahead". Astern, swing to whichever side it leans, starboard if neither.
     * Returns (roll, pitch, yaw) rates for SetFlightCommand. Pitch about +Y
     * puts the nose down, so "up" is a negative rate.
     */
    FVector SteerByWords(const FString& Line)
    {
        if (Line.Contains(TEXT("dead ahead")))
        {
            return FVector::ZeroVector;
        }
        const auto Rate = [](double Degrees) { return FMath::Clamp(Degrees / 20.0, 0.1, 1.0); };
        FVector Attitude = FVector::ZeroVector;
        if (Line.Contains(TEXT("astern")))
        {
            Attitude.Z = Line.Contains(TEXT("to port")) ? -1.0 : 1.0;
            return Attitude;
        }
        if (Line.Contains(TEXT("to port")))
        {
            Attitude.Z = -Rate(DegreesBefore(Line, TEXT("to port")));
        }
        else if (Line.Contains(TEXT("to starboard")))
        {
            Attitude.Z = Rate(DegreesBefore(Line, TEXT("to starboard")));
        }
        if (Line.Contains(TEXT(" up")))
        {
            Attitude.Y = -Rate(DegreesBefore(Line, TEXT(" up")));
        }
        else if (Line.Contains(TEXT(" down")))
        {
            Attitude.Y = Rate(DegreesBefore(Line, TEXT(" down")));
        }
        return Attitude;
    }

    /** Every DistantStars instance, in the counter-frame's own space: the
     *  dome as the galaxy sees it, before the actor's rotation. */
    TArray<FVector> DomeDirections(const AShipCounterFrame* Frame)
    {
        TArray<FVector> Out;
        const UInstancedStaticMeshComponent* Dome = Frame->GetDistantStars();
        for (int32 Index = 0; Index < Dome->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Dome->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ false);
            Out.Add(Instance.GetLocation().GetSafeNormal());
        }
        return Out;
    }

    /** Custom-data float Slot of Instance. */
    float CustomData(const UInstancedStaticMeshComponent* Layer, int32 Instance, int32 Slot)
    {
        return Layer->PerInstanceSMCustomData[Instance * Layer->NumCustomDataFloats + Slot];
    }

    /** Whether nothing on this component can reach the interior's lighting
     *  or its captures (sky decision 5). */
    bool KeptOutOfTheInterior(const UPrimitiveComponent* Component)
    {
        return Component && !Component->bVisibleInReflectionCaptures && !Component->bVisibleInRealTimeSkyCaptures
            && !Component->bAffectDynamicIndirectLighting && !Component->bAffectDistanceFieldLighting
            && !Component->bVisibleInRayTracing;
    }
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceLoopJumpTest,
    "DeepSpace.Loop.Jump",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * Cruise, choose, jump, arrive, with the sky and the counter-frame drawing
 * every frame: plotted from the console, engaged, charged while pointed
 * elsewhere, aimed by the HUD's words alone, and left to fire with no
 * confirm. Then the fold -- the sky gone, the streaks -- and the arrival:
 * the ship is in the destination because its position says so, the sky has
 * rebuilt for the new serial, a different sun is ahead, the old home is now
 * a neighbour, and the galaxy behind it all has not moved.
 */
bool FSliceLoopJumpTest::RunTest(const FString& Parameters)
{
    using namespace SliceLoopTestLocal;

    FSkyWorld Test(TEXT("SliceLoopJumpWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("a universe"), Test.Universe)
        || !TestNotNull(TEXT("a counter-frame"), Test.Frame) || !TestNotNull(TEXT("and a sky"), Test.Sky))
    {
        return false;
    }
    Test.BeginPlay();
    Test.Step(0.0f);

    UShipSubsystem* Ship = Test.Ship;
    AShipSky* Sky = Test.Sky;
    AShipCounterFrame* Frame = Test.Frame;
    const FSystemId Home = Test.Universe->GetStartSystem();

    TestEqual(TEXT("the sky is built for the opening serial"), Sky->GetBuiltForSerial(), 0);
    TestTrue(TEXT("nothing outside the hull reaches the interior's captures: the dome"),
             KeptOutOfTheInterior(Frame->GetDistantStars()));
    TestTrue(TEXT("and the motes"), KeptOutOfTheInterior(Frame->GetNearStars()));
    const TArray<FVector> Galaxy = DomeDirections(Frame);

    // Chosen from the console, as slice 1 chooses.
    const TArray<FStarSystemStub> Chart = Ship->GetChart();
    if (!TestTrue(TEXT("the chart has somewhere to go"), Chart.Num() > 0))
    {
        return false;
    }
    {
        FOutputDeviceNull Quiet;
        IConsoleManager::Get().ProcessUserConsoleInput(TEXT("ds.Nav.Plot 0"), Quiet, Test.World);
    }
    const FSystemId Destination = Chart[0].Id;
    TestTrue(TEXT("ds.Nav.Plot 0 plots the nearest system"),
             Ship->GetPlottedSystem().IsSet() && *Ship->GetPlottedSystem() == Destination);
    const TOptional<FVector> Course = Ship->GetCourseDirection();
    if (!TestTrue(TEXT("with a direction"), Course.IsSet()))
    {
        return false;
    }

    // Pointed well away from it, so charging and aiming are separate acts.
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(-*Course).ToQuat());
    Test.Step(0.0f);
    UStaticMeshComponent* Marker = Frame->GetCourseMarker();
    if (TestNotNull(TEXT("plotting puts the course marker on the dome"), Marker))
    {
        TestTrue(TEXT("shown"), Marker->IsVisible());
        TestTrue(TEXT("where the course is"),
                 Marker->GetRelativeLocation().GetSafeNormal().Equals(*Course, 1e-6));
        TestTrue(TEXT("and kept out of the interior's captures"), KeptOutOfTheInterior(Marker));
    }

    TestTrue(TEXT("the jump engages"), Ship->SetJumpEngaged(true));
    double Waited = 0.0;
    while (Waited < 300.0 && Ship->GetJumpState() != EJumpState::Ready)
    {
        Test.Step(0.5f);
        Waited += 0.5;
    }
    TestEqual(TEXT("it charges to ready"), static_cast<int32>(Ship->GetJumpState()), static_cast<int32>(EJumpState::Ready));
    TestFalse(TEXT("and, pointed away, holds there"), Ship->IsInTransit());
    TestEqual(TEXT("so the sky has not rebuilt"), Sky->GetBuiltForSerial(), 0);

    // Aimed by the words in the corner and nothing else. The only calls
    // from here to the fold are the pilot's hands on the helm: there is no
    // confirm to make.
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    FString LastLine;
    double Steering = 0.0;
    while (Steering < 120.0 && !Ship->IsInTransit())
    {
        LastLine = UShipHUDWidget::DriveLineText(*Ship, Test.Universe).ToString();
        Ship->SetFlightCommand(Pilot, 0.0f, SteerByWords(LastLine));
        Test.Step(1.0f / 30.0f);
        Steering += 1.0 / 30.0;
    }
    TestTrue(FString::Printf(TEXT("aimed by the HUD alone, the jump fires by itself (last read: \"%s\", %.1f s)"), *LastLine, Steering),
             Ship->IsInTransit());

    // The fold: nowhere to be near.
    Test.Step(0.1f);
    TestTrue(TEXT("in transit the sky hides every body"), AllProxies(Sky, false));
    TestFalse(TEXT("and the sun"), Sky->GetSun()->IsVisible());
    TestFalse(TEXT("and the neighbours"), Sky->GetNeighbourStars()->IsVisible());
    TestFalse(TEXT("the counter-frame hides the dome"), Frame->GetDistantStars()->IsVisible());
    TestTrue(TEXT("and the marker"), !Marker || !Marker->IsVisible());
    TestTrue(TEXT("and the motes are the streaks"), Frame->GetNearStars()->IsVisible());

    double Transit = 0.0;
    while (Transit < 60.0 && Ship->GetJumpSerial() == 0)
    {
        Test.Step(0.25f);
        Transit += 0.25;
    }
    TestEqual(TEXT("it arrives"), Ship->GetJumpSerial(), 1);

    // Where it is, asked of where it is (plan conflict 1).
    const FUniversePosition Arrived = Ship->GetFlightState().GetUniversePosition();
    const TOptional<FStarSystem> There = Test.Universe->GetSystemAt(Arrived);
    if (!TestTrue(TEXT("GetSystemAt the ship's position is the destination"), There.IsSet() && There->Stub.Id == Destination))
    {
        return false;
    }

    // The sky followed the serial by asking, and drew somewhere else.
    TestEqual(TEXT("the sky rebuilt for the new serial"), Sky->GetBuiltForSerial(), 1);
    TestEqual(TEXT("and so did the counter-frame's near field"), Frame->GetBuiltForSerial(), 1);
    TestEqual(TEXT("one proxy for the new star and each of its planets"), Sky->GetProxyCount(), 1 + There->Planets.Num());
    TestTrue(TEXT("every one shown"), AllProxies(Sky, true));
    TestTrue(TEXT("the new sun is up"), Sky->GetSun()->IsVisible());
    TestTrue(TEXT("the neighbours are back"), Sky->GetNeighbourStars()->IsVisible() && Sky->GetNeighbourStars()->GetInstanceCount() > 0);
    TestTrue(TEXT("and the dome"), Frame->GetDistantStars()->IsVisible());
    TestTrue(TEXT("the course marker is gone with the course"), !Marker || !Marker->IsVisible());

    if (Sky->GetProxy(0))
    {
        const FVector Ahead = Sky->GetProxy(0)->GetComponentLocation().GetSafeNormal();
        TestTrue(FString::Printf(TEXT("a different sun is ahead, inside the jump cone (%.2f deg off the nose)"),
                                 FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Ahead.X, -1.0, 1.0)))),
                 Ahead.X >= FMath::Cos(Ship->GetJumpConeRadians()));
    }

    // Home is a neighbour now, where it really is from here.
    const TOptional<FStarSystem> Left = Test.Universe->GetSystem(Home);
    if (TestTrue(TEXT("home still exists"), Left.IsSet()))
    {
        const FVector Back = Ship->UniverseToWorld(Left->Stub.Position).GetSafeNormal();
        bool bHomeInSky = false;
        const UInstancedStaticMeshComponent* Neighbours = Sky->GetNeighbourStars();
        for (int32 Index = 0; Index < Neighbours->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Neighbours->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ true);
            bHomeInSky |= (Instance.GetLocation() - Sky->GetActorLocation()).GetSafeNormal().Equals(Back, 1e-6);
        }
        TestTrue(TEXT("the system left behind is a neighbour, drawn where it is from the ship"), bHomeInSky);
    }

    // The unmoved background is what makes the rest read as elsewhere.
    const TArray<FVector> After = DomeDirections(Frame);
    bool bGalaxyStill = After.Num() == Galaxy.Num() && Galaxy.Num() > 0;
    for (int32 Index = 0; bGalaxyStill && Index < Galaxy.Num(); ++Index)
    {
        bGalaxyStill = After[Index].Equals(Galaxy[Index], 1e-9);
    }
    TestTrue(TEXT("the galaxy behind it all has not moved"), bGalaxyStill);
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceLoopDriveTest,
    "DeepSpace.Loop.Drive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The drive, flown from the opening shot with the sky drawing it (sky
 * decision 8, plan conflict 10). Toward the framed planet it closes
 * exponentially and settles at the floor, and the planet grows without ever
 * shrinking or jumping. Away from everything it runs out to the system's
 * edge and stops short of it: the drive never takes the ship out of its
 * system -- you leave by jumping.
 */
bool FSliceLoopDriveTest::RunTest(const FString& Parameters)
{
    using namespace SliceLoopTestLocal;

    FSkyWorld Test(TEXT("SliceLoopDriveWorld"));
    if (!TestNotNull(TEXT("the world has a ship"), Test.Ship) || !TestNotNull(TEXT("and a sky"), Test.Sky))
    {
        return false;
    }
    Test.BeginPlay();
    Test.Step(0.0f);

    UShipSubsystem* Ship = Test.Ship;
    const FSystemId HomeId = Test.Universe->GetStartSystem();
    const TOptional<FStarSystem> Home = Test.Universe->GetSystem(HomeId);
    const int32 Largest = Home ? LargestPlanet(*Home) : INDEX_NONE;
    if (!TestTrue(TEXT("home has a planet to close on"), Largest != INDEX_NONE))
    {
        return false;
    }
    const double Radius = Home->Planets[Largest].RadiusEarth * UniverseUnits::CmPerEarthRadius;
    const FUniversePosition Planet = Home->PlanetPosition(Largest);
    const auto Altitude = [&]() { return Ship->GetFlightState().GetUniversePosition().DistanceTo(Planet) - Radius; };
    const auto Seen = [&]() { return Subtense(Drawn(Test.Sky->GetProxy(1 + Largest), Test.Sphere), PilotEye); };

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetFlightCommand(Pilot, 1.0f, FVector::ZeroVector);
    TestTrue(TEXT("the pilot engages the drive"), Ship->SetDriveEngaged(Pilot, true));

    // Closing: ten time constants, at a frame rate a player might have.
    const double Tau = Ship->GetFlightState().GetLimits().DriveTau;
    const double Floor = Ship->GetFlightState().GetLimits().DriveFloor;
    const double Start = Altitude();
    double Previous = Start;
    double PreviousSeen = Seen();
    bool bAlwaysCloser = true;
    bool bNeverShrinks = true;
    bool bNeverBelowFloor = true;
    bool bAlwaysHome = true;
    for (double Time = 0.0; Time < 10.0 * Tau; Time += 0.25)
    {
        Test.Step(0.25f);
        const double Now = Altitude();
        const double NowSeen = Seen();
        bAlwaysCloser &= Now < Previous;
        bNeverShrinks &= NowSeen >= PreviousSeen;
        bNeverBelowFloor &= Now >= Floor - 1.0;
        const TOptional<FStarSystem> Here = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
        bAlwaysHome &= Here.IsSet() && Here->Stub.Id == HomeId;
        Previous = Now;
        PreviousSeen = NowSeen;
    }
    TestTrue(TEXT("closing, every frame is nearer the planet"), bAlwaysCloser);
    TestTrue(TEXT("and the planet never looks smaller"), bNeverShrinks);
    TestTrue(TEXT("and the ship never passes the floor"), bNeverBelowFloor);
    TestTrue(TEXT("and it is home throughout"), bAlwaysHome);
    const double Room = (Altitude() - Floor) / (Start - Floor);
    TestTrue(FString::Printf(TEXT("ten tau on, the room is e^-10 of what it was, within a factor of 2: %.3g"), Room),
             Room > 0.5 * FMath::Exp(-10.0) && Room < 2.0 * FMath::Exp(-10.0));
    {
        const FUniversePosition Eye = Ship->GetFlightState().WorldToUniverse(PilotEye);
        const double True = 2.0 * FMath::Asin(Radius / (Planet - Eye).Size());
        TestTrue(FString::Printf(TEXT("at the floor it fills the glass at its true size: %.2f deg against %.2f"),
                                 FMath::RadiansToDegrees(Seen()), FMath::RadiansToDegrees(True)),
                 FMath::Abs(Seen() / True - 1.0) < 1e-3);
    }

    // Leaving: nose straight up off the planet, lever left on. Straight up,
    // because the room is the altitude: skimming the horizon at the floor
    // gains height only as the square of the distance run, and the drive
    // crawls at cruise for minutes before it opens up.
    const FUniversePosition Star = Home->Stub.Position;
    const FVector Out = (Ship->GetFlightState().GetUniversePosition() - Planet).GetSafeNormal();
    Ship->PlaceShip(Ship->GetFlightState().GetUniversePosition(), FRotationMatrix::MakeFromX(Out).ToQuat());
    bAlwaysHome = true;
    for (double Time = 0.0; Time < 60.0 * Tau; Time += 0.5)
    {
        Ship->Tick(0.5f);
        const TOptional<FStarSystem> Here = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
        bAlwaysHome &= Here.IsSet() && Here->Stub.Id == HomeId;
    }
    const double Reach = Ship->GetFlightState().GetUniversePosition().DistanceTo(Star) / FStarSystem::InSystemRadiusCm;
    TestTrue(TEXT("flown outward for sixty tau, the ship never leaves its system"), bAlwaysHome);
    TestTrue(FString::Printf(TEXT("though it went nearly all the way to the edge: %.6f of the way"), Reach),
             Reach > 0.99 && Reach < 1.0);
    Test.Step(0.0f);
    TestEqual(TEXT("and the sky still draws home"), Test.Sky->GetProxyCount(), 1 + Home->Planets.Num());
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceLoopPointStarsTest,
    "DeepSpace.Loop.PointStars",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * A neighbour and a background star of the same flux are the same point.
 * The counter-frame draws the galaxy and the sky draws the neighbours; both
 * must go through AShipSky::PointStarBrightness, on the same shell, or the
 * destinations either vanish into the backdrop or shout over it.
 */
bool FSliceLoopPointStarsTest::RunTest(const FString& Parameters)
{
    using namespace SliceLoopTestLocal;

    TestEqual(TEXT("the counter-frame's dome and the sky's are one shell"),
              GetDefault<AShipCounterFrame>()->DistantStarRadius, AShipSky::DomeRadius);

    FSkyWorld Test(TEXT("SliceLoopPointStarsWorld"), /*DistantStarCount*/ 64);
    if (!TestNotNull(TEXT("the world has a counter-frame"), Test.Frame) || !TestNotNull(TEXT("and a sky"), Test.Sky))
    {
        return false;
    }
    FScopedCVar Faint(TEXT("ds.Sky.StarfieldFaint"), 0.01f);
    FScopedCVar Radiance(TEXT("ds.Sky.Radiance"), 3.0f);
    FScopedCVar Gamma(TEXT("ds.Sky.FluxGamma"), 0.5f);
    Test.BeginPlay();
    Test.Step(0.0f);

    const UInstancedStaticMeshComponent* Dome = Test.Frame->GetDistantStars();
    const UInstancedStaticMeshComponent* Neighbours = Test.Sky->GetNeighbourStars();
    const TArray<FSkyStar> Galaxy = SkyStarfield::Generate(
        LocalSystem::StarfieldSeed(Test.World, static_cast<uint64>(Test.Frame->StarSeed)), Test.Frame->DistantStarCount);
    if (!TestEqual(TEXT("the dome is the galaxy the seed makes"), Dome->GetInstanceCount(), Galaxy.Num())
        || !TestTrue(TEXT("and there are neighbours"), Neighbours->GetInstanceCount() > 0))
    {
        return false;
    }

    // The brightest background star, so the compression has room to show.
    int32 Bright = 0;
    for (int32 Index = 1; Index < Galaxy.Num(); ++Index)
    {
        Bright = Galaxy[Index].Flux > Galaxy[Bright].Flux ? Index : Bright;
    }
    const double F = Galaxy[Bright].Flux;
    const float Drawn = CustomData(Dome, Bright, SkyMaterial::CustomDataBrightness);
    TestEqual(TEXT("a background star's brightness is PointStarBrightness of its flux"), Drawn, AShipSky::PointStarBrightness(F));
    TestTrue(FString::Printf(TEXT("which is sqrt(F) x 0.01 x 3: %.5f against %.5f at flux %.1f"), Drawn, FMath::Sqrt(F) * 0.03, F),
             FMath::IsNearlyEqual(Drawn, FMath::Sqrt(F) * 0.03, 1e-5));

    // The nearest neighbour, as the sky refers it to the ship.
    const FSkySystem Here = LocalSystem::Current(Test.World);
    const FSkyBody* Star = Here.Bodies.FindByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
    if (TestNotNull(TEXT("home has a star"), Star) && TestTrue(TEXT("and neighbours"), Here.Neighbours.Num() > 0))
    {
        const FVector StarFromShip = Star->Position - Test.Ship->GetFlightState().GetUniversePosition();
        const double N = ShipSky::NeighbourFlux(ShipSky::NeighbourFromShip(Here.Neighbours[0], StarFromShip));
        const float Point = CustomData(Neighbours, 0, SkyMaterial::CustomDataBrightness);
        TestTrue(FString::Printf(TEXT("a neighbour of flux %.1f draws as a background star of that flux would: %.5f against %.5f"),
                                 N, Point, FMath::Sqrt(N) * 0.03),
                 FMath::IsNearlyEqual(Point, FMath::Sqrt(N) * 0.03, 1e-5));
    }

    // Tuned in play, the dome follows the frame the neighbours do.
    {
        FScopedCVar Brighter(TEXT("ds.Sky.Radiance"), 6.0f);
        Test.Step(0.0f);
        TestTrue(TEXT("doubling ds.Sky.Radiance doubles the dome on the next frame"),
                 FMath::IsNearlyEqual(CustomData(Dome, Bright, SkyMaterial::CustomDataBrightness), 2.0f * Drawn, 1e-5f));
    }
    return true;
}

#endif
