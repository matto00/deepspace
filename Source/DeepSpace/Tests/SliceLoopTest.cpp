#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDeviceNull.h"
#include "Ship/ShipDressingSubsystem.h"
#include "Ship/NavStart.h"
#include "Ship/ShipNavState.h"
#include "Sky/LocalSystem.h"
#include "Sky/SkyProjection.h"
#include "Sky/SkyStarfield.h"
#include "Tests/HaulerDressingMarkers.h"
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

    /** Every DistantStars instance as drawn, turned back into universe axes
     *  through the ship's attitude: the galaxy as the universe sees it. The
     *  ship turning moves every star on screen; this does not move unless
     *  the galaxy itself did. */
    TArray<FVector> GalaxyDirections(const AShipCounterFrame* Frame, const UShipSubsystem* Ship)
    {
        TArray<FVector> Out;
        const UInstancedStaticMeshComponent* Dome = Frame->GetDistantStars();
        const FQuat Attitude = Ship->GetFlightState().GetUniverseOrientation();
        for (int32 Index = 0; Index < Dome->GetInstanceCount(); ++Index)
        {
            FTransform Instance;
            Dome->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ true);
            Out.Add(Attitude.RotateVector((Instance.GetLocation() - Frame->GetActorLocation()).GetSafeNormal()));
        }
        return Out;
    }

    /** Custom-data float Slot of Instance. */
    /** How wide an instance is drawn, cm: its scale times its mesh's own
     *  width, read from the mesh's bounds and never assumed. */
    double Across(const UInstancedStaticMeshComponent* Layer, int32 Instance)
    {
        FTransform Transform;
        Layer->GetInstanceTransform(Instance, Transform, /*bWorldSpace*/ false);
        const UStaticMesh* Mesh = Layer->GetStaticMesh();
        return Mesh ? Transform.GetScale3D().X * 2.0 * Mesh->GetBoundingBox().GetExtent().GetMax() : 0.0;
    }

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
    // Somebody's ship: the hauler's real dressing surfaces, placed before
    // play as the level build places them, so the whole loop runs with the
    // clutter aboard and must come out the other side with it unchanged.
    HaulerDressingMarkers::FMarkers Markers;
    FString MarkersError;
    const bool bMarkers = HaulerDressingMarkers::Load(Markers, MarkersError);
    if (!TestTrue(TEXT("the hauler's dressing markers load ") + MarkersError, bMarkers))
    {
        return false;
    }
    HaulerDressingMarkers::Spawn(Test.World, Markers);
    Test.BeginPlay();
    Test.Step(0.0f);

    UShipSubsystem* Ship = Test.Ship;
    AShipSky* Sky = Test.Sky;
    AShipCounterFrame* Frame = Test.Frame;
    const FSystemId Home = Test.Universe->GetStartSystem();

    UShipDressingSubsystem* Dressing = Test.World->GetSubsystem<UShipDressingSubsystem>();
    const TMap<FString, TArray<FTransform>> Dressed = HaulerDressingMarkers::Drawing(Dressing ? Dressing->GetClutter() : nullptr);
    TestTrue(FString::Printf(TEXT("the ship is dressed when play begins (%d instances)"), Dressing ? Dressing->GetInstanceCount() : 0),
             Dressing && Dressing->GetInstanceCount() > 0);

    TestEqual(TEXT("the sky is built for the opening serial"), Sky->GetBuiltForSerial(), 0);
    TestTrue(TEXT("nothing outside the hull reaches the interior's captures: the dome"),
             KeptOutOfTheInterior(Frame->GetDistantStars()));
    TestTrue(TEXT("and the motes"), KeptOutOfTheInterior(Frame->GetNearStars()));
    const TArray<FVector> Galaxy = GalaxyDirections(Frame, Ship);

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
    // With the drive's lever left at its top, 0.1 c, while aiming, as a pilot might: the
    // fold is an all stop, and the ship must arrive at rest, both levers at
    // STOP (flight-feel decision 4) -- not fly the arrival at the new star.
    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    Ship->SetDriveEngaged(Pilot, true);
    Ship->SetDriveLever(Pilot, Ship->GetFlightState().GetDriveNotchCount() - 1);
    FString LastLine;
    double Steering = 0.0;
    while (Steering < 120.0 && !Ship->IsInTransit())
    {
        LastLine = UShipHUDWidget::JumpLineText(*Ship, Test.Universe).ToString();
        Ship->SetFlightCommand(Pilot, 0.0f, SteerByWords(LastLine));
        Test.Step(1.0f / 30.0f);
        Steering += 1.0 / 30.0;
    }
    TestTrue(FString::Printf(TEXT("aimed by the HUD alone, the jump fires by itself (last read: \"%s\", %.1f s)"), *LastLine, Steering),
             Ship->IsInTransit());

    TestTrue(FString::Printf(TEXT("under way under the drive as the fold opens (%.0f km/s)"), Ship->GetShipSpeed() / 1.0e5),
             Ship->GetShipSpeed() > Ship->GetFlightState().GetLimits().MaxSpeed);
    TestTrue(TEXT("and the fold puts both levers at STOP"),
             Ship->GetFlightState().GetCommand().DriveNotch == 0 && Ship->GetFlightState().GetCommand().Throttle == 0.0);

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
    TestTrue(TEXT("at rest, exactly"), Ship->GetFlightState().GetVelocity().IsZero());
    TestTrue(TEXT("with both levers at STOP"),
             Ship->GetFlightState().GetCommand().DriveNotch == 0 && Ship->GetFlightState().GetCommand().Throttle == 0.0);

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

    // Every neighbour where it really is from the ship -- not from the new
    // star, which the arrival leaves a few AU away -- and home among them.
    // Home alone would not show the difference: the arrival lands on the
    // line from home to the new star, so from there the two agree.
    const TOptional<FStarSystem> Left = Test.Universe->GetSystem(Home);
    const FSkySystem Now = LocalSystem::Current(Test.World);
    const UInstancedStaticMeshComponent* Neighbours = Sky->GetNeighbourStars();
    if (TestTrue(TEXT("home still exists"), Left.IsSet()) && TestFalse(TEXT("and the new system is drawn"), Now.IsEmpty())
        && TestEqual(TEXT("one point per neighbour"), Neighbours->GetInstanceCount(), Now.Neighbours.Num()))
    {
        const FUniversePosition NewStar = Now.Bodies[0].Position;
        bool bHomeInSky = false;
        bool bEachFromTheShip = Now.Neighbours.Num() > 1;
        for (int32 Index = 0; Index < Now.Neighbours.Num(); ++Index)
        {
            const FSkyNeighbour& Neighbour = Now.Neighbours[Index];
            const FUniversePosition Where = NewStar + Neighbour.Direction * Neighbour.Distance;
            FTransform Instance;
            Neighbours->GetInstanceTransform(Index, Instance, /*bWorldSpace*/ false);
            bEachFromTheShip &= Instance.GetLocation().GetSafeNormal().Equals((Where - Arrived).GetSafeNormal(), 1e-9);
            bHomeInSky |= Where.DistanceTo(Left->Stub.Position) < UniverseUnits::CmPerKm;
        }
        TestTrue(TEXT("the system left behind is a neighbour now"), bHomeInSky);
        TestTrue(TEXT("and every neighbour is drawn where it is from the ship, to 1e-9"), bEachFromTheShip);
    }

    // The unmoved background is what makes the rest read as elsewhere.
    const TArray<FVector> After = GalaxyDirections(Frame, Ship);
    bool bGalaxyStill = After.Num() == Galaxy.Num() && Galaxy.Num() > 0;
    for (int32 Index = 0; bGalaxyStill && Index < Galaxy.Num(); ++Index)
    {
        bGalaxyStill = After[Index].Equals(Galaxy[Index], 1e-6);
    }
    TestTrue(TEXT("the galaxy behind it all has not moved"), bGalaxyStill);

    // Nothing aboard belongs to where the ship is. The dressing is drawn
    // from the ship's markers and the universe's root, never from the system
    // it arrives in or how many jumps it took, so dressed again here -- as
    // ds.Dress.* redresses, and as the runtime ship generator one day will --
    // it is the ship that left: no new mess per port, nothing to tidy however
    // far it goes (the anti-chore principle). Asked by redressing, because a
    // dressing that never ticks cannot change by itself, and a check that it
    // had not would be a check of nothing.
    if (Dressing)
    {
        Dressing->Redress();
    }
    TestTrue(TEXT("and dressed again where it arrived, the ship is dressed exactly as it left"),
             !Dressed.IsEmpty() && Dressing && HaulerDressingMarkers::SameDrawing(Dressed, HaulerDressingMarkers::Drawing(Dressing->GetClutter())));
    return true;
}

// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSliceLoopDriveTest,
    "DeepSpace.Loop.Drive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The drive, flown from the opening shot with the sky drawing it
 * (flight-feel decisions 3-6). The lever from STOP to 0.1 c with the nose on
 * the framed planet: the ship closes, the planet grows without ever
 * shrinking, and within about a minute it is on the floor -- the sky's own
 * rendered floor, where the world is drawn at its true size -- and at rest.
 * Aimed a hundredth of a degree off, the same floor in the same time. And
 * flown out at the system's edge, it settles just inside it: the drive never
 * takes the ship out of its system -- you leave by jumping.
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
    const FSkySystem Here = LocalSystem::Here(Test.World);
    const double Radius = Home->Planets[Largest].RadiusEarth * UniverseUnits::CmPerEarthRadius;
    const FUniversePosition Planet = Home->PlanetPosition(Largest);
    if (!TestTrue(TEXT("the sky's body for it is the planet"), Here.Bodies.IsValidIndex(1 + Largest)
                  && FMath::IsNearlyEqual(Here.Bodies[1 + Largest].Radius, Radius, 1.0)))
    {
        return false;
    }
    const double Floor = UShipSubsystem::FloorFor(Here.Bodies[1 + Largest]);
    const auto Altitude = [&]() { return Ship->GetFlightState().GetUniversePosition().DistanceTo(Planet) - Radius; };
    const auto Seen = [&]() { return Subtense(Drawn(Test.Sky->GetProxy(1 + Largest), Test.Sphere), PilotEye); };
    const FNavPlacement Opening = NavStart::OpeningPlacement(*Home);

    APawn* Pilot = Test.World->SpawnActor<APawn>();
    Ship->SetPilot(Pilot);
    TestTrue(TEXT("the pilot engages the drive"), Ship->SetDriveEngaged(Pilot, true));
    const int32 Top = Ship->GetFlightState().GetDriveNotchCount() - 1;

    // Closing, at a frame rate a player might have, from STOP.
    const auto Approach = [&](const FQuat& Nose, bool bWatch)
    {
        Ship->PlaceShip(Opening.Position, Nose);
        Ship->SetDriveLever(Pilot, Top);
        double Previous = Altitude();
        double PreviousSeen = Seen();
        bool bAlwaysCloser = true;
        bool bNeverShrinks = true;
        bool bNeverBelowFloor = true;
        bool bAlwaysHome = true;
        double Arrived = -1.0;
        for (double Time = 0.25; Time <= 90.0; Time += 0.25)
        {
            Test.Step(0.25f);
            const double Now = Altitude();
            bAlwaysCloser &= Now <= Previous;
            bNeverBelowFloor &= Now >= Floor - 1.0;
            if (bWatch)
            {
                const double NowSeen = Seen();
                bNeverShrinks &= NowSeen >= PreviousSeen;
                PreviousSeen = NowSeen;
                const TOptional<FStarSystem> At = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
                bAlwaysHome &= At.IsSet() && At->Stub.Id == HomeId;
            }
            if (Arrived < 0.0 && Now - Floor <= FShipFlightState::AtFloorCm)
            {
                Arrived = Time;
            }
            Previous = Now;
        }
        TestTrue(TEXT("closing, no frame is farther from the planet"), bAlwaysCloser);
        TestTrue(TEXT("and the ship never passes the floor"), bNeverBelowFloor);
        if (bWatch)
        {
            TestTrue(TEXT("the planet never looks smaller"), bNeverShrinks);
            TestTrue(TEXT("and it is home throughout"), bAlwaysHome);
        }
        return Arrived;
    };

    const double Arrived = Approach(Opening.Orientation, true);
    TestTrue(FString::Printf(TEXT("from the opening shot at 0.1 c, on the floor within 70 s (%.2f s)"), Arrived),
             Arrived > 0.0 && Arrived <= 70.0);
    TestTrue(FString::Printf(TEXT("the floor is the sky's rendered floor (%.1f km)"), Floor / UniverseUnits::CmPerKm),
             Floor == FMath::Max(10.0 * UniverseUnits::CmPerKm, SkyProjection::RenderedFloor(Radius, FSkyViewParams())));
    TestTrue(TEXT("and at rest on it"), Ship->GetShipSpeed() < 1.0f);
    {
        const FUniversePosition Eye = Ship->GetFlightState().WorldToUniverse(PilotEye);
        const double True = 2.0 * FMath::Asin(Radius / (Planet - Eye).Size());
        TestTrue(FString::Printf(TEXT("at the floor it fills the glass at its true size: %.2f deg against %.2f"),
                                 FMath::RadiansToDegrees(Seen()), FMath::RadiansToDegrees(True)),
                 FMath::Abs(Seen() / True - 1.0) < 1e-3);
    }

    // A hundredth of a degree off the centre: the same floor, the same time.
    Ship->AllStop(Pilot);
    Test.Step(0.25f);
    const double Offset = Approach(Opening.Orientation * FQuat(FVector::UpVector, FMath::DegreesToRadians(0.01)), false);
    TestTrue(FString::Printf(TEXT("a hundredth of a degree off, on the floor in the same time to a second: %.2f s against %.2f"),
                             Offset, Arrived),
             Offset > 0.0 && FMath::Abs(Offset - Arrived) <= 1.0);

    // Out at the edge. Placed 0.01 AU inside it and flown out at 0.1 c, which
    // uncapped would carry it 0.012 AU in the first minute: it stays home,
    // and settles on the edge's floor, 10 km inside.
    Ship->AllStop(Pilot);
    Test.Step(0.25f);
    const FUniversePosition Star = Home->Stub.Position;
    const FVector Out = (Opening.Position - Star).GetSafeNormal();
    const double Edge = FStarSystem::InSystemRadiusCm;
    Ship->PlaceShip(Star + Out * (Edge - 0.01 * UniverseUnits::CmPerAU), FRotationMatrix::MakeFromX(Out).ToQuat());
    Ship->SetDriveLever(Pilot, Top);
    bool bAlwaysHome = true;
    double Farthest = 0.0;
    for (double Time = 0.5; Time <= 150.0; Time += 0.5)
    {
        Ship->Tick(0.5f);
        const TOptional<FStarSystem> At = Test.Universe->GetSystemAt(Ship->GetFlightState().GetUniversePosition());
        bAlwaysHome &= At.IsSet() && At->Stub.Id == HomeId;
        Farthest = FMath::Max(Farthest, Ship->GetFlightState().GetUniversePosition().DistanceTo(Star));
    }
    const double Inside = Edge - Ship->GetFlightState().GetUniversePosition().DistanceTo(Star);
    TestTrue(TEXT("flown out at the edge at 0.1 c, the ship never leaves its system"), bAlwaysHome);
    TestTrue(FString::Printf(TEXT("never past the edge's floor (%.1f m inside the edge at most)"), (Edge - Farthest) / 100.0),
             Farthest <= Edge - UShipSubsystem::EdgeFloor() + 1000.0);
    TestTrue(FString::Printf(TEXT("and settles on it, 10 km inside: %.3f km"), Inside / UniverseUnits::CmPerKm),
             FMath::Abs(Inside - UShipSubsystem::EdgeFloor()) <= 1000.0 && Ship->GetShipSpeed() < 1.0f);
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
    FScopedCVar Size(TEXT("ds.Sky.PointPixels"), 2.0f);
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

    // And the same size: a destination found by its brightness, never by
    // being a bigger dot than the galaxy behind it.
    const double Point = Across(Neighbours, 0);
    const double TwoPixels = ShipSky::PointDiameter(AShipSky::DomeRadius, Test.Sky->GetPixelAngle(), 2.0);
    TestTrue(FString::Printf(TEXT("a neighbour is drawn two pixels across: %.1f cm against %.1f"), Point, TwoPixels),
             FMath::IsNearlyEqual(Point, TwoPixels, 1e-6 * TwoPixels));
    TestTrue(FString::Printf(TEXT("and so is a background star: %.1f cm against %.1f"), Across(Dome, Bright), Point),
             FMath::IsNearlyEqual(Across(Dome, Bright), Point, 1e-6 * Point));

    // Tuned in play, the dome follows the frame the neighbours do.
    {
        FScopedCVar Brighter(TEXT("ds.Sky.Radiance"), 6.0f);
        Test.Step(0.0f);
        TestTrue(TEXT("doubling ds.Sky.Radiance doubles the dome on the next frame"),
                 FMath::IsNearlyEqual(CustomData(Dome, Bright, SkyMaterial::CustomDataBrightness), 2.0f * Drawn, 1e-5f));
    }
    // Settled back before each next tuning: Radiance going back to 3 moves
    // the brightness key, and a rebuild on its account would re-light and
    // resize the dome whether or not the next tuning is keyed at all.
    Test.Step(0.0f);
    {
        // Flux 1 compresses to 1 at any gamma, so only the brightest star
        // shows this one move.
        FScopedCVar Honest(TEXT("ds.Sky.FluxGamma"), 1.0f);
        Test.Step(0.0f);
        TestTrue(TEXT("ds.Sky.FluxGamma 1 draws the brightest star at its honest flux on the next frame"),
                 FMath::IsNearlyEqual(CustomData(Dome, Bright, SkyMaterial::CustomDataBrightness), static_cast<float>(F * 0.03), 1e-4f));
    }
    Test.Step(0.0f);
    {
        // What the CVar's own help suggests when two-pixel points shimmer
        // under TSR: it must reach the 3,000 stars, not only the destinations.
        FScopedCVar Bigger(TEXT("ds.Sky.PointPixels"), 3.0f);
        Test.Step(0.0f);
        const double Larger = Across(Neighbours, 0);
        TestTrue(FString::Printf(TEXT("ds.Sky.PointPixels 3 draws a neighbour half as wide again: %.1f cm against %.1f"),
                                 Larger, 1.5 * Point),
                 FMath::IsNearlyEqual(Larger, 1.5 * Point, 1e-3 * Point));
        TestTrue(FString::Printf(TEXT("and the dome with it, on the same frame: %.1f cm against %.1f"), Across(Dome, Bright), Larger),
                 FMath::IsNearlyEqual(Across(Dome, Bright), Larger, 1e-6 * Larger));
    }
    return true;
}

#endif
