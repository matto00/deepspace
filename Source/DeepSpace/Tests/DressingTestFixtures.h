#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipDressing.h"

/**
 * The hauler's dressing surfaces and keep-out zones as literals, typed in
 * from Tools/hauler_layout.py's generate() (ship.surfaces, ship.keep_outs).
 * Test data, not a generator -- the rule SkyTestFixtures.h follows (plan
 * conflict 4). If the layout's surfaces change shape this is updated by
 * hand; nothing breaks if it lags, because every check is made against each
 * surface's own size and zones, whatever they are.
 * Test scaffolding: nothing outside Tests/ may include this.
 */
namespace DressingFixtures
{
    inline FDressSurface Surface(const TCHAR* Room, const TCHAR* Kind, int32 Ordinal, const FVector& At, double Yaw,
                                 const FVector2D& Size, EDressEdge Back, EDressUse Use, float Clear,
                                 TArray<FBox2D> Excludes = {})
    {
        FDressSurface Out;
        Out.Room = FName(Room);
        Out.Kind = FName(Kind);
        Out.Ordinal = Ordinal;
        Out.ToWorld = FTransform(FRotator(0.0, Yaw, 0.0).Quaternion(), At);
        Out.Size = Size;
        Out.Back = Back;
        Out.Use = Use;
        Out.Clear = Clear;
        Out.Excludes = MoveTemp(Excludes);
        return Out;
    }

    inline FBox2D Rect(double X0, double Y0, double X1, double Y1)
    {
        return FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1));
    }

    /** Every surface the hauler exports: one of every kind, the desk and the
     *  workbench with a lamp's exclude, the galley table with the laptop's,
     *  the starboard wing with the chart's, three shelves tall enough to
     *  pile on. */
    inline TArray<FDressSurface> Hauler()
    {
        using E = EDressEdge;
        using U = EDressUse;
        return {
            Surface(TEXT("cockpit"), TEXT("cockpit_desk.wing_port"), 0, FVector(1645, -155, 80), 0, FVector2D(100, 50), E::PosX, U::Centre, 40),
            Surface(TEXT("cockpit"), TEXT("cockpit_desk.wing_stbd"), 0, FVector(1645, 155, 80), 0, FVector2D(100, 50), E::PosX, U::Centre, 40,
                    { Rect(-50, -25, 50, -21) }),
            Surface(TEXT("cargo_bay"), TEXT("wall_rack.shelf_0"), 0, FVector(-160, -375, 77.5), 90, FVector2D(50, 180), E::NegX, U::Centre, 70),
            Surface(TEXT("cargo_bay"), TEXT("wall_rack.shelf_1"), 0, FVector(-160, -375, 152.5), 90, FVector2D(50, 180), E::NegX, U::Centre, 70),
            Surface(TEXT("cargo_bay"), TEXT("wall_rack.shelf_2"), 0, FVector(-160, -375, 227.5), 90, FVector2D(50, 180), E::NegX, U::Centre, 70),
            Surface(TEXT("engineering"), TEXT("workbench.top"), 0, FVector(760, 180, 90), 180, FVector2D(80, 160), E::NegX, U::Centre, 40,
                    { Rect(-40, -80, 25, -40), Rect(-40, -27, -34, 27) }),
            Surface(TEXT("galley"), TEXT("galley_table.top"), 0, FVector(1110, 310, 79), 90, FVector2D(90, 180), E::NegX, U::Centre, 40,
                    { Rect(-45, -30, 30, 30) }),
            Surface(TEXT("bunk"), TEXT("locker.top"), 0, FVector(960, -350, 200), 90, FVector2D(60, 60), E::NegX, U::Centre, 45),
            Surface(TEXT("bunk"), TEXT("desk.top"), 0, FVector(930, -160, 77), 180, FVector2D(60, 120), E::NegX, U::NegY, 40,
                    { Rect(-30, -60, 21, -20) }),
            Surface(TEXT("airlock"), TEXT("airlock_bench.seat"), 0, FVector(330, -270, 45), 0, FVector2D(40, 120), E::NegX, U::NegY, 30),
            Surface(TEXT("galley"), TEXT("counter.top"), 0, FVector(840, 280, 90), 0, FVector2D(60, 300), E::NegX, U::PosY, 58),
        };
    }

    /** The hauler's keep-outs: every door's keep-clear zone, the console's,
     *  the slide run, the crawlway. */
    inline TArray<FBox> HaulerKeepOut()
    {
        return {
            FBox(FVector(1300, -80, 0), FVector(1510, 70, 220)),
            FBox(FVector(-110, -80, 0), FVector(100, 70, 240)),
            FBox(FVector(540, -30, 0), FVector(660, 180, 220)),
            FBox(FVector(980, -30, 0), FVector(1130, 180, 220)),
            FBox(FVector(200, -190, 0), FVector(320, 20, 220)),
            FBox(FVector(740, -190, 0), FVector(860, 20, 220)),
            FBox(FVector(-110, 390, 0), FVector(100, 480, 150)),
            FBox(FVector(290, 390, 0), FVector(500, 480, 150)),
            FBox(FVector(550, 372, 0), FVector(650, 472, 180)),
            FBox(FVector(0, -80, 0), FVector(1400, 70, 250)),
            FBox(FVector(0, 390, 0), FVector(390, 480, 150)),
        };
    }

    /**
     * Surfaces the hauler does not have, each built to prove one guard can
     * bite: a long counter reaching into a doorway's keep-clear zone (the
     * zone covers its +Y half), a table standing on the floor, and a shelf
     * too low for most of what a shelf holds.
     */
    inline FDressSurface IntoTheDoorway()
    {
        return Surface(TEXT("probe"), TEXT("counter.top"), 7, FVector(1000, 300, 90), 0, FVector2D(60, 400),
                       EDressEdge::NegX, EDressUse::PosY, 58);
    }

    inline FBox TheDoorway()
    {
        return FBox(FVector(900, 300, 0), FVector(1100, 600, 220));
    }

    inline FDressSurface OnTheFloor()
    {
        return Surface(TEXT("probe"), TEXT("counter.top"), 8, FVector(1000, -300, 0), 0, FVector2D(60, 300),
                       EDressEdge::NegX, EDressUse::PosY, 58);
    }

    /** A rack shelf with the next shelf 5 cm above it: room for a coil of
     *  cable or one book, and nothing else in the rack's mix. The hauler has
     *  no surface this low under its clearance, so without this nothing
     *  would ever test that the clear keeps a canister off a shelf. */
    inline FDressSurface UnderALowShelf()
    {
        return Surface(TEXT("probe"), TEXT("wall_rack.shelf_9"), 9, FVector(-400, -375, 120), 90, FVector2D(50, 180),
                       EDressEdge::NegX, EDressUse::Centre, 5);
    }
}
