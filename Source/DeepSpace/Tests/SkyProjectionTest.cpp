#include "Misc/AutomationTest.h"
#include "Sky/SkyColour.h"
#include "Sky/SkyMaterialContract.h"
#include "Sky/SkyProjection.h"
#include "Tests/SkyTestFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    /** A 4K pixel at 90 degrees: what "under a pixel" means in the spec. */
    constexpr double Pixel4K = 2.0 / 3840.0;

    constexpr double AU = 1.495978707e13;

    /** Direction the ship approaches every body from in the sweeps: off every
     *  axis, so no component is special. */
    FVector ApproachDirection()
    {
        return FVector(-0.8, 0.45, 0.4).GetSafeNormal();
    }

    double Floor(const FSkyBody& Body, const FSkyViewParams& Params)
    {
        return FMath::Max(Params.MinRenderedAltitude, Params.MinRenderedAltitudeOfRadius * Body.Radius);
    }

    /** Ship positions from Body's rendered-altitude floor out to 30 AU,
     *  log-spaced: every decade of the approach is sampled alike. */
    TArray<FUniversePosition> Sweep(const FSkyBody& Body, const FSkyViewParams& Params, int32 Steps = 48)
    {
        TArray<FUniversePosition> Ships;
        const double Near = Floor(Body, Params);
        const double Far = 30.0 * AU;
        for (int32 Step = 0; Step < Steps; ++Step)
        {
            const double T = static_cast<double>(Step) / (Steps - 1);
            const double Altitude = Near * FMath::Pow(Far / Near, T);
            Ships.Add(Body.Position + ApproachDirection() * (Body.Radius + Altitude));
        }
        return Ships;
    }

    double ProxyNear(const FSkyBodyView& View) { return View.ProxyLocation.Size() - View.ProxyRadius; }
    double ProxyFar(const FSkyBodyView& View) { return View.ProxyLocation.Size() + View.ProxyRadius; }

    /** The angle between two vectors, accurate for tiny angles. */
    double AngleBetween(const FVector& A, const FVector& B)
    {
        return FMath::Atan2(FVector::CrossProduct(A, B).Size(), FVector::DotProduct(A, B));
    }

    /** Distance along unit U from the origin to the near side of a sphere,
     *  or a negative number if the ray misses. */
    double RayHit(const FVector& U, const FVector& Centre, double Radius)
    {
        const double B = FVector::DotProduct(U, Centre);
        const double C = Centre.SizeSquared() - Radius * Radius;
        const double Discriminant = B * B - C;
        if (Discriminant < 0.0)
        {
            return -1.0;
        }
        return B - FMath::Sqrt(Discriminant);
    }

    double RelativeError(double Actual, double Expected)
    {
        return FMath::Abs(Actual - Expected) / FMath::Max(FMath::Abs(Expected), UE_DOUBLE_SMALL_NUMBER);
    }

    FSkyBody MakeStar(const FUniversePosition& Where)
    {
        FSkyBody Star;
        Star.Id = TEXT("Star");
        Star.Kind = ESkyBodyKind::Star;
        Star.Position = Where;
        Star.Radius = 6.957e10;
        Star.TemperatureK = 5772.0;
        return Star;
    }

    FSkyBody MakeBody(const TCHAR* Id, const FUniversePosition& Where, double Radius, ESkyBodyKind Kind = ESkyBodyKind::Planet)
    {
        FSkyBody Body;
        Body.Id = Id;
        Body.Kind = Kind;
        Body.Position = Where;
        Body.Radius = Radius;
        return Body;
    }

    /**
     * Every pair's proxy depth intervals are disjoint, and wherever two true
     * discs overlap on screen, the body a ray meets first is drawn wholly in
     * front of the other and earlier in DepthOrder.
     */
    void CheckOcclusion(FAutomationTestBase& Test, const TCHAR* Case, const FSkySystem& System,
                        const FUniversePosition& Ship, const FSkyViewParams& Params)
    {
        const FSkyFrame Frame = SkyProjection::Project(System, Ship, Params);
        const int32 Count = Frame.Bodies.Num();

        TArray<int32> Rank;
        Rank.SetNum(Count);
        for (int32 Order = 0; Order < Count; ++Order)
        {
            Rank[Frame.DepthOrder[Order]] = Order;
        }

        for (int32 A = 0; A < Count; ++A)
        {
            for (int32 B = A + 1; B < Count; ++B)
            {
                const FSkyBodyView& ViewA = Frame.Bodies[A];
                const FSkyBodyView& ViewB = Frame.Bodies[B];
                const bool bDisjoint = ProxyFar(ViewA) < ProxyNear(ViewB) || ProxyFar(ViewB) < ProxyNear(ViewA);
                Test.TestTrue(FString::Printf(TEXT("%s: %s and %s have disjoint proxy depths"), Case,
                    *System.Bodies[A].Id.ToString(), *System.Bodies[B].Id.ToString()), bDisjoint);

                const double Separation = AngleBetween(ViewA.Direction, ViewB.Direction);
                if (Separation >= ViewA.AngularRadius + ViewB.AngularRadius)
                {
                    continue;
                }

                // The overlap region crosses the arc between the two centres,
                // so rays along that arc find it.
                const FVector CentreA = System.Bodies[A].Position - Ship;
                const FVector CentreB = System.Bodies[B].Position - Ship;
                int32 Checked = 0;
                for (int32 Step = 0; Step <= 64; ++Step)
                {
                    const double T = Step / 64.0;
                    const FVector U = ((1.0 - T) * ViewA.Direction + T * ViewB.Direction).GetSafeNormal();
                    const double HitA = RayHit(U, CentreA, System.Bodies[A].Radius);
                    const double HitB = RayHit(U, CentreB, System.Bodies[B].Radius);
                    if (HitA < 0.0 || HitB < 0.0)
                    {
                        continue;
                    }
                    ++Checked;
                    const int32 First = HitA < HitB ? A : B;
                    const int32 Second = First == A ? B : A;
                    if (ProxyFar(Frame.Bodies[First]) >= ProxyNear(Frame.Bodies[Second]) || Rank[First] >= Rank[Second])
                    {
                        Test.AddError(FString::Printf(TEXT("%s: a ray meets %s first, but it is not drawn in front of %s"),
                            Case, *System.Bodies[First].Id.ToString(), *System.Bodies[Second].Id.ToString()));
                        break;
                    }
                }
                Test.AddInfo(FString::Printf(TEXT("%s: %s/%s overlap on screen, %d rays checked"), Case,
                    *System.Bodies[A].Id.ToString(), *System.Bodies[B].Id.ToString(), Checked));
            }
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyProjectionTest,
    "DeepSpace.Sky.Projection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyProjectionTest::RunTest(const FString& Parameters)
{
    const FSkyViewParams Params;
    const FSkySystem Fixture = SkyTestFixtures::System();
    const double MinDrawnRadius = 0.5 * Params.MinPointPixels * Params.PixelAngle;

    // Angular size is exact from the ship's origin -- or exactly a point's
    // two pixels -- for every body, from its floor to 30 AU. This pins the
    // homothety.
    {
        double Worst = 0.0;
        for (const FSkyBody& Approached : Fixture.Bodies)
        {
            for (const FUniversePosition& Ship : Sweep(Approached, Params))
            {
                const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
                for (const FSkyBodyView& View : Frame.Bodies)
                {
                    const double Drawn = FMath::Asin(View.ProxyRadius / View.ProxyLocation.Size());
                    const double Expected = FMath::Max(View.AngularRadius, MinDrawnRadius);
                    Worst = FMath::Max(Worst, RelativeError(Drawn, Expected));
                    Worst = FMath::Max(Worst, AngleBetween(View.ProxyLocation, View.Direction));
                }
            }
        }
        AddInfo(FString::Printf(TEXT("worst angular-size or direction error from the origin: %.3g"), Worst));
        TestTrue(TEXT("every proxy subtends its true angular size (or a point's) in its true direction, to 1e-9"), Worst < 1.0e-9);
    }

    // It is right from where the player stands. With the eye 20 m off the
    // origin along each axis, every body's direction is within a 4K pixel and
    // its angular radius within 0.1% of the truth wherever its near surface
    // is beyond NearProxy, and within 4 px down to the floor. The first
    // draft's 2 km near edge fails this by a factor of 25.
    {
        const TArray<FVector> Eyes = {
            FVector(2000.0, 0.0, 0.0), FVector(-2000.0, 0.0, 0.0),
            FVector(0.0, 2000.0, 0.0), FVector(0.0, -2000.0, 0.0),
            FVector(0.0, 0.0, 2000.0), FVector(0.0, 0.0, -2000.0) };
        double WorstFar = 0.0;
        double WorstNear = 0.0;
        double WorstSize = 0.0;
        for (const FSkyBody& Approached : Fixture.Bodies)
        {
            for (const FUniversePosition& Ship : Sweep(Approached, Params))
            {
                const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
                for (int32 Index = 0; Index < Frame.Bodies.Num(); ++Index)
                {
                    const FSkyBody& Body = Fixture.Bodies[Index];
                    const FSkyBodyView& View = Frame.Bodies[Index];
                    const bool bBeyondNear = View.Distance - Body.Radius > Params.NearProxy;
                    for (const FVector& Eye : Eyes)
                    {
                        const FVector TrueRelative = (Body.Position - Ship) - Eye;
                        const FVector ProxyRelative = View.ProxyLocation - Eye;
                        const double Error = AngleBetween(TrueRelative, ProxyRelative);
                        (bBeyondNear ? WorstFar : WorstNear) = FMath::Max(bBeyondNear ? WorstFar : WorstNear, Error);

                        if (bBeyondNear && View.AngularRadius >= MinDrawnRadius)
                        {
                            const double TrueSize = FMath::Asin(Body.Radius / TrueRelative.Size());
                            const double ProxySize = FMath::Asin(View.ProxyRadius / ProxyRelative.Size());
                            WorstSize = FMath::Max(WorstSize, RelativeError(ProxySize, TrueSize));
                        }
                    }
                }
            }
        }
        AddInfo(FString::Printf(TEXT("eye 20 m off: worst direction %.3g px beyond NearProxy, %.3g px nearer; worst size %.3g"),
            WorstFar / Pixel4K, WorstNear / Pixel4K, WorstSize));
        TestTrue(TEXT("beyond NearProxy, every body is within a 4K pixel of true from the eye"), WorstFar < Pixel4K);
        TestTrue(TEXT("down to the floor, within 4 pixels"), WorstNear < 4.0 * Pixel4K);
        TestTrue(TEXT("beyond NearProxy, angular size from the eye is true to 0.1%"), WorstSize < 1.0e-3);
    }

    // Direction is exact with the ship and the body in different chunks.
    {
        const double Chunk = FUniversePosition::ChunkSize;
        const FUniversePosition Ship(FInt64Vector(7, 0, 0), FVector(Chunk - 100.0, 5.0, 0.0));
        FSkySystem System;
        System.Bodies.Add(MakeStar(FUniversePosition(FInt64Vector(7, 3, 0), FVector::ZeroVector)));
        System.Bodies.Add(MakeBody(TEXT("Across"), FUniversePosition(FInt64Vector(8, 0, 0), FVector(900.0, 5.0, 0.0)), 10.0));
        const FSkyFrame Frame = SkyProjection::Project(System, Ship, Params);
        TestEqual(TEXT("a body across a chunk boundary is dead ahead"), Frame.Bodies[1].Direction, FVector(1.0, 0.0, 0.0));
        TestEqual(TEXT("at its true distance"), Frame.Bodies[1].Distance, 1000.0);
    }

    // Occlusion survives every case that broke a draft.
    {
        const FUniversePosition Origin;
        FSkySystem Behind;
        Behind.Bodies.Add(MakeStar(Origin + FVector(0.0, 1.5e13, 0.0)));
        Behind.Bodies.Add(MakeBody(TEXT("Planet"), Origin + FVector(1.0e10, 0.0, 0.0), 6.4e8));
        Behind.Bodies.Add(MakeBody(TEXT("Moon"), Origin + FVector(1.3e10, 1.0e8, 0.0), 1.7e8, ESkyBodyKind::Moon));
        CheckOcclusion(*this, TEXT("a moon behind its planet"), Behind, Origin, Params);

        FSkySystem Transit;
        Transit.Bodies.Add(MakeStar(Origin + FVector(1.5e13, 0.0, 0.0)));
        Transit.Bodies.Add(MakeBody(TEXT("Planet"), Origin + FVector(5.0e12, 1.0e8, 0.0), 6.4e8));
        CheckOcclusion(*this, TEXT("a planet transiting its star"), Transit, Origin, Params);

        // 150 km over the home planet's night side: the star, and everything
        // else round it, is behind the planet.
        const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
        const FVector AwayFromStar = (Home.Position - Fixture.Bodies[SkyTestFixtures::StarIndex].Position).GetSafeNormal();
        CheckOcclusion(*this, TEXT("150 km over a planet, the system behind it"), Fixture,
            Home.Position + AwayFromStar * (Home.Radius + 1.5e7), Params);

        // The review's counterexample: a planet at d = 100, R = 50 and a small
        // moon at d = 60, 28 degrees off the planet's centre -- inside its
        // disc, outside the planet, and truly in front of it.
        FSkySystem Counter;
        Counter.Bodies.Add(MakeStar(Origin + FVector(-1.5e13, 0.0, 0.0)));
        Counter.Bodies.Add(MakeBody(TEXT("Planet"), Origin + FVector(1.0e9, 0.0, 0.0), 5.0e8));
        const double Off = FMath::DegreesToRadians(28.0);
        Counter.Bodies.Add(MakeBody(TEXT("Moon"), Origin + FVector(6.0e8 * FMath::Cos(Off), 6.0e8 * FMath::Sin(Off), 0.0), 1.0e7, ESkyBodyKind::Moon));
        TestTrue(TEXT("the counterexample bites: by d - R the moon is behind the planet"),
            6.0e8 - 1.0e7 > 1.0e9 - 5.0e8);
        CheckOcclusion(*this, TEXT("the review's counterexample"), Counter, Origin, Params);
        const FSkyFrame Frame = SkyProjection::Project(Counter, Origin, Params);
        TestTrue(TEXT("the moon is drawn in front of the planet"),
            Frame.DepthOrder.IndexOfByKey(2) < Frame.DepthOrder.IndexOfByKey(1));
    }

    // The band holds: every proxy in [NearProxy, FarProxy], for the fixture
    // approached to every body's floor. The test that fails first if someone
    // gives a giant twenty moons.
    {
        double Nearest = TNumericLimits<double>::Max();
        double Farthest = 0.0;
        for (const FSkyBody& Approached : Fixture.Bodies)
        {
            for (const FUniversePosition& Ship : Sweep(Approached, Params))
            {
                const FSkyFrame Frame = SkyProjection::Project(Fixture, Ship, Params);
                for (const FSkyBodyView& View : Frame.Bodies)
                {
                    Nearest = FMath::Min(Nearest, ProxyNear(View));
                    Farthest = FMath::Max(Farthest, ProxyFar(View));
                }
            }
        }
        AddInfo(FString::Printf(TEXT("band used: %.4g to %.4g cm (%.0fx of %.0fx)"),
            Nearest, Farthest, Farthest / Nearest, Params.FarProxy / Params.NearProxy));
        TestTrue(TEXT("no proxy nearer than NearProxy"), Nearest >= Params.NearProxy * (1.0 - 1.0e-9));
        TestTrue(TEXT("no proxy farther than FarProxy"), Farthest <= Params.FarProxy);
    }

    // The resolve. One Earth at 1 AU from its star, the ship closing on it
    // along the line it is seen at half phase from.
    {
        const FUniversePosition Origin;
        FSkySystem System;
        System.Bodies.Add(MakeStar(Origin + FVector(0.0, AU, 0.0)));
        FSkyBody Earth = MakeBody(TEXT("Earth"), Origin, 6.3781e8);
        Earth.Albedo = 0.3;
        System.Bodies.Add(Earth);

        const auto ShipAtPixels = [&](double Pixels)
        {
            const double Radius = 0.5 * Pixels * Params.PixelAngle;
            return Origin + FVector(-Earth.Radius / FMath::Sin(Radius), 0.0, 0.0);
        };
        const auto Flux = [](const FSkyBodyView& View)
        {
            return View.Brightness * SkyProjection::ConeSolidAngle(View.DrawnAngularRadius)
                * (View.PointBlend + (1.0 - View.PointBlend) * View.Phase);
        };

        double LastBlend = 1.0;
        bool bMonotone = true;
        bool bPointBelow = true;
        bool bDiscAbove = true;
        bool bBoostOnlyWhenInflated = true;
        for (double Pixels = 0.05; Pixels < 12.0; Pixels *= 1.05)
        {
            const FSkyBodyView View = SkyProjection::Project(System, ShipAtPixels(Pixels), Params).Bodies[1];
            bMonotone &= View.PointBlend <= LastBlend;
            LastBlend = View.PointBlend;
            if (Pixels <= Params.MinPointPixels)
            {
                bPointBelow &= View.PointBlend == 1.0;
                bBoostOnlyWhenInflated &= Pixels == Params.MinPointPixels ? View.PointBoost == 1.0 : View.PointBoost > 1.0;
            }
            else
            {
                bBoostOnlyWhenInflated &= View.PointBoost == 1.0;
            }
            if (Pixels >= Params.MinPointPixels + Params.ResolveBandPixels)
            {
                bDiscAbove &= View.PointBlend == 0.0;
            }
        }
        TestTrue(TEXT("PointBlend is 1 below 2 px"), bPointBelow);
        TestTrue(TEXT("PointBlend is 0 above 4 px"), bDiscAbove);
        TestTrue(TEXT("PointBlend never rises as the body grows"), bMonotone);
        TestTrue(TEXT("PointBoost is above 1 only while the body is drawn larger than it is"), bBoostOnlyWhenInflated);

        FSkyViewParams Honest = Params;
        Honest.FluxGamma = 1.0;
        bool bNoBoost = true;
        for (double Pixels = 0.05; Pixels < 12.0; Pixels *= 1.2)
        {
            bNoBoost &= SkyProjection::Project(System, ShipAtPixels(Pixels), Honest).Bodies[1].PointBoost == 1.0;
        }
        TestTrue(TEXT("with FluxGamma 1 nothing is boosted"), bNoBoost);

        const double Below = Flux(SkyProjection::Project(System, ShipAtPixels(Params.MinPointPixels * (1.0 - 1.0e-12)), Params).Bodies[1]);
        const double Above = Flux(SkyProjection::Project(System, ShipAtPixels(Params.MinPointPixels * (1.0 + 1.0e-12)), Params).Bodies[1]);
        AddInfo(FString::Printf(TEXT("flux either side of 2 px: %.12g, %.12g"), Below, Above));
        TestTrue(TEXT("the displayed flux is continuous at 2 px, to 1e-9"), RelativeError(Below, Above) < 1.0e-9);

        // Through the band the flux is exactly the honest one: the point and
        // disc terms are the same light.
        bool bHonestInBand = true;
        for (double Pixels = 2.0; Pixels <= 4.0; Pixels += 0.125)
        {
            const FSkyBodyView View = SkyProjection::Project(System, ShipAtPixels(Pixels), Params).Bodies[1];
            const double Expected = View.SurfaceBrightness * View.Phase * SkyProjection::ConeSolidAngle(View.AngularRadius);
            bHonestInBand &= RelativeError(Flux(View), Expected) < 1.0e-9;
        }
        TestTrue(TEXT("through the resolve the flux is the honest one"), bHonestInBand);
    }

    // Surfaces are honest: once resolved, a planet and a star look as bright
    // per pixel at any distance. The first draft's approach dimmed 300x.
    {
        const FSkyBody& Home = Fixture.Bodies[SkyTestFixtures::HomeIndex];
        const FSkyBody& Star = Fixture.Bodies[SkyTestFixtures::StarIndex];
        for (const int32 Index : { SkyTestFixtures::HomeIndex, SkyTestFixtures::StarIndex })
        {
            const FSkyBody& Body = Fixture.Bodies[Index];
            // Along one ray from the body, so the phase is fixed too.
            const FVector Out = (Index == SkyTestFixtures::HomeIndex ? (Home.Position - Star.Position) : ApproachDirection()).GetSafeNormal()
                .RotateAngleAxis(60.0, FVector::UpVector);
            double First = -1.0;
            double FirstSurface = -1.0;
            double Worst = 0.0;
            int32 Resolved = 0;
            for (double Altitude = Floor(Body, Params); Altitude < 5.0 * AU; Altitude *= 1.7)
            {
                const FSkyBodyView View = SkyProjection::Project(Fixture, Body.Position + Out * (Body.Radius + Altitude), Params).Bodies[Index];
                if (View.PointBlend > 0.0)
                {
                    continue;
                }
                ++Resolved;
                if (First < 0.0)
                {
                    First = View.Brightness;
                    FirstSurface = View.SurfaceBrightness;
                }
                Worst = FMath::Max(Worst, RelativeError(View.Brightness, First));
                Worst = FMath::Max(Worst, RelativeError(View.SurfaceBrightness, FirstSurface));
                Worst = FMath::Max(Worst, RelativeError(View.Brightness, View.SurfaceBrightness));
            }
            TestTrue(FString::Printf(TEXT("%s is resolved over many distances"), *Body.Id.ToString()), Resolved > 10);
            TestTrue(FString::Printf(TEXT("%s: resolved brightness is independent of distance, to 1e-12"), *Body.Id.ToString()), Worst < 1.0e-12);
        }
    }

    // Compression and the phase law.
    {
        TestTrue(TEXT("a 900x ratio becomes 30x at gamma 0.5"), RelativeError(SkyProjection::Compress(900.0, 0.5), 30.0) < 1.0e-12);
        bool bMonotone = true;
        for (double X = 1.0e-4; X < 1.0e4; X *= 1.3)
        {
            bMonotone &= SkyProjection::Compress(X * 1.3, 0.5) > SkyProjection::Compress(X, 0.5);
        }
        TestTrue(TEXT("Compress is monotone"), bMonotone);
        TestEqual(TEXT("nothing compresses to nothing"), SkyProjection::Compress(0.0, 0.5), 0.0);

        TestTrue(TEXT("full phase is 1"), RelativeError(SkyProjection::LambertPhase(0.0), 1.0) < 1.0e-12);
        TestTrue(TEXT("half phase is 1/pi"), RelativeError(SkyProjection::LambertPhase(0.5 * UE_DOUBLE_PI), 1.0 / UE_DOUBLE_PI) < 1.0e-12);
        TestTrue(TEXT("new phase is dark"), FMath::Abs(SkyProjection::LambertPhase(UE_DOUBLE_PI)) < 1.0e-12);

        // M_SkyBody's shaded term, LambertDiscGain * saturate(N.L), averaged
        // over the visible disc, is LambertPhase: integrate it on a grid.
        double Worst = 0.0;
        for (double Alpha = 0.0; Alpha <= UE_DOUBLE_PI; Alpha += UE_DOUBLE_PI / 8.0)
        {
            const FVector Light(FMath::Sin(Alpha), 0.0, FMath::Cos(Alpha));
            double Sum = 0.0;
            int32 Samples = 0;
            constexpr int32 Grid = 400;
            for (int32 I = 0; I < Grid; ++I)
            {
                for (int32 J = 0; J < Grid; ++J)
                {
                    const double X = -1.0 + (I + 0.5) * 2.0 / Grid;
                    const double Y = -1.0 + (J + 0.5) * 2.0 / Grid;
                    const double R2 = X * X + Y * Y;
                    if (R2 >= 1.0)
                    {
                        continue;
                    }
                    const FVector Normal(X, Y, FMath::Sqrt(1.0 - R2));
                    Sum += SkyMaterial::LambertDiscGain * FMath::Max(FVector::DotProduct(Normal, Light), 0.0);
                    ++Samples;
                }
            }
            Worst = FMath::Max(Worst, FMath::Abs(Sum / Samples - SkyProjection::LambertPhase(Alpha)));
        }
        AddInfo(FString::Printf(TEXT("disc-averaged shading against LambertPhase: worst %.3g"), Worst));
        TestTrue(TEXT("the material's shaded term averages to LambertPhase over the disc"), Worst < 2.0e-3);
    }

    // The sun: its direction and irradiance from the opening, and no star
    // means no sunlight rather than a sun from nowhere.
    {
        const FSkyFrame Frame = SkyProjection::Project(Fixture, SkyTestFixtures::Opening(), Params);
        TestTrue(TEXT("from the opening the sun is off to +Y"), AngleBetween(Frame.SunDirection, FVector(0.0, 1.0, 0.0)) < 1.0e-9);
        TestTrue(TEXT("a Sun at 1 AU gives irradiance 1"), RelativeError(Frame.SunIrradiance, 1.0) < 1.0e-9);
        TestEqual(TEXT("nothing is eclipsed until the eclipse term lands"), Frame.SunVisibleFraction, 1.0);

        FSkySystem Starless;
        Starless.Bodies.Add(MakeBody(TEXT("Rogue"), FUniversePosition() + FVector(1.0e10, 0.0, 0.0), 6.4e8));
        const FSkyFrame Dark = SkyProjection::Project(Starless, FUniversePosition(), Params);
        TestEqual(TEXT("no star, no sunlight"), Dark.SunIrradiance, 0.0);
        TestEqual(TEXT("and an unlit world"), Dark.Bodies[0].Brightness, 0.0);

        const FSkyFrame Empty = SkyProjection::Project(FSkySystem(), FUniversePosition(), Params);
        TestEqual(TEXT("an empty system projects to nothing"), Empty.Bodies.Num(), 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyColourTest,
    "DeepSpace.Sky.Colour",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkyColourTest::RunTest(const FString& Parameters)
{
    const FLinearColor RedDwarf = SkyColour::Blackbody(3200.0);
    const FLinearColor Sun = SkyColour::Blackbody(5772.0);
    const FLinearColor Hot = SkyColour::Blackbody(12000.0);

    TestTrue(TEXT("a red dwarf is red"), RedDwarf.R > RedDwarf.G && RedDwarf.G > RedDwarf.B);
    TestTrue(TEXT("a hot star is blue"), Hot.B > Hot.R);
    TestTrue(TEXT("the Sun is near white"), Sun.GetMin() > 0.8f);
    for (const FLinearColor& Colour : { RedDwarf, Sun, Hot, SkyColour::Blackbody(800.0), SkyColour::Blackbody(40000.0) })
    {
        TestTrue(TEXT("a blackbody is a chromaticity: its brightest channel is 1"), FMath::IsNearlyEqual(Colour.GetMax(), 1.0f));
        TestTrue(TEXT("and no channel is negative"), Colour.GetMin() >= 0.0f);
    }
    return true;
}

#endif
