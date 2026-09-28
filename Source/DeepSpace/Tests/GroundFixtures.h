#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ship/ShipFlightSurface.h"
#include "Sky/SkySystem.h"
#include "Surface/GroundField.h"
#include "Universe/UniverseSubsystem.h"
#include "Universe/UniverseUnits.h"

/**
 * Grounds for the landing tests: a cheap analytic one with a chosen slope
 * bound, a fixed relief, and the real worlds of home by orbit. Data, not a
 * generator: the real worlds come from the universe as the game builds it.
 */
namespace GroundFixtures
{
    /**
     * Two crossed sines about the +Z pole, h = A (sin(k R D.x) + sin(k R D.y)) / 2:
     * arc lengths R D.x and R D.y, so it is only meaningful near the pole,
     * where every test that uses it flies. Smooth, a few nanoseconds a sample,
     * and with the slope bound set to whatever the test needs -- the real
     * relief's, for GroundAlwaysCatches (spec decision 10).
     */
    class FCrossedSines final : public IGroundField
    {
    public:
        FCrossedSines(double InRadiusCm, double InAmplitudeCm, double InWavelengthCm)
            : R(InRadiusCm), A(InAmplitudeCm), K(2.0 * UE_DOUBLE_PI / InWavelengthCm)
        {
        }

        /** The same field with its steepest slope, rise over run, set to Slope. */
        static FCrossedSines WithSlope(double InRadiusCm, double InWavelengthCm, double Slope)
        {
            const double K = 2.0 * UE_DOUBLE_PI / InWavelengthCm;
            return FCrossedSines(InRadiusCm, 2.0 * Slope / (K * UE_DOUBLE_SQRT_2), InWavelengthCm);
        }

        virtual double RadiusCm() const override { return R; }

        virtual double Height(const FVector3d& D, double) const override
        {
            return 0.5 * A * (FMath::Sin(K * R * D.X) + FMath::Sin(K * R * D.Y));
        }

        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            Grad = FVector3d(0.5 * A * K * R * FMath::Cos(K * R * D.X), 0.5 * A * K * R * FMath::Cos(K * R * D.Y), 0.0);
            return Height(D, FootprintCm);
        }

        virtual double OmittedBoundCm(double) const override { return 0.0; }
        virtual double MaxHeightCm() const override { return FMath::Abs(A); }
        virtual double MinHeightCm() const override { return -FMath::Abs(A); }
        virtual double MaxSlope() const override { return 0.5 * FMath::Abs(A) * K * UE_DOUBLE_SQRT_2; }

    private:
        double R;
        double A;
        double K;
    };

    /** A barren world of 0.9 Earth radii with a 6.4 km peak, craters kept:
     *  the shape of Baemsekai IV without needing the universe. */
    inline FWorldReliefParams FixtureParams()
    {
        FWorldReliefParams Params;
        Params.SeedOffset = FVector3d(12.5, 40.25, 7.75);
        Params.RadiusCm = 0.9 * UniverseUnits::CmPerEarthRadius;
        Params.PeakCm = 6.4e5;
        Params.Cratering = 1.0;
        Params.Ground = EGround::Solid;
        return Params;
    }

    /** Off the universe origin and across chunks, as the real galaxy is. */
    inline FUniversePosition Somewhere()
    {
        return FUniversePosition(FInt64Vector(3, -2, 0), FVector(1.0e12, 5.0e12, 0.0));
    }

    /** A solid world's surface: the drive floor sphere at DriveFloorCm over
     *  the datum, and the ground. */
    inline FFlightSurface SurfaceOver(const FGroundFieldRef& Ground, double DriveFloorCm,
                                      const FUniversePosition& Centre = Somewhere())
    {
        FFlightSurface Surface;
        Surface.Centre = Centre;
        Surface.Radius = Ground->RadiusCm();
        Surface.Floor = DriveFloorCm;
        Surface.bWorld = true;
        Surface.Ground = Ground;
        return Surface;
    }

    /** AglCm above the ground at direction D (unit, universe axes). */
    inline FUniversePosition Above(const FFlightSurface& Surface, const FVector3d& D, double AglCm)
    {
        return Surface.Centre + FVector(D) * (Surface.Radius + Surface.Ground->Height(D, 0.0) + AglCm);
    }

    /** Level over D: body +Z along the radial up, the nose along Heading
     *  projected onto the horizon. */
    inline FQuat Level(const FVector3d& D, const FVector& Heading = FVector(1.0, 0.0, 0.0))
    {
        const FVector Up(D);
        return FRotationMatrix::MakeFromXZ(Heading - Up * (Heading | Up), Up).ToQuat();
    }

    /** A world of home by orbit (I is 1): seed 20260925's start system,
     *  Baemsekai, as the game builds it from the ini's seed and priors. The
     *  caller checks the name, so a priors change that moves the fixture
     *  fails loudly rather than landing somewhere else (spec, Fixture worlds). */
    struct FHomeWorld
    {
        FSkyBody Body;
        FGroundFieldRef Ground;
        FString Name;
    };

    inline TOptional<FHomeWorld> HomeWorld(int32 Orbit)
    {
        UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("GroundFixtureWorld"));
        FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
        Context.SetCurrentWorld(World);
        TOptional<FHomeWorld> Out;
        if (const UUniverseSubsystem* Universe = World->GetSubsystem<UUniverseSubsystem>())
        {
            if (const TOptional<FStarSystem> Home = Universe->GetSystem(Universe->GetStartSystem()))
            {
                const FSkySystem Sky = FSkySystem::FromSystem(*Home, {});
                if (Sky.Bodies.IsValidIndex(Orbit) && Sky.Bodies[Orbit].Ground == EGround::Solid)
                {
                    FHomeWorld Found;
                    Found.Body = Sky.Bodies[Orbit];
                    Found.Ground = ShipGround::FromRelief(Found.Body.Relief);
                    Found.Name = Found.Body.Id.ToString();
                    Out = Found;
                }
            }
        }
        GEngine->DestroyWorldContext(World);
        World->DestroyWorld(false);
        return Out;
    }
}
