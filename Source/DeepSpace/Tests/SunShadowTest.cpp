#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"
#include "Surface/GroundField.h"
#include "Surface/SunShadow.h"
#include "Surface/WorldRelief.h"
#include "Tests/GroundFixtures.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowKnownValuesTest, "DeepSpace.Surface.SunShadow.KnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowExitsTest, "DeepSpace.Surface.SunShadow.Exits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowScheduleTest, "DeepSpace.Surface.SunShadow.Schedule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowAgainstProfileTest, "DeepSpace.Surface.SunShadow.AgainstProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSunShadowSteepestSlopeTest, "DeepSpace.Surface.SunShadow.SteepestSlope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace SunShadowTestLocal
{
    using namespace SunShadow;

    /** Ground that is a plane tilted K (rise over run) up toward Up, as far
     *  as a sphere allows: Height(D) = K R (D . Up). Where D is square to Up
     *  its slope along the ground is K exactly, and nowhere more. */
    class FTiltedGround final : public IGroundField
    {
    public:
        FTiltedGround(double InRadiusCm, double InK, const FVector3d& InUp) : R(InRadiusCm), K(InK), Up(InUp) {}
        virtual double RadiusCm() const override { return R; }
        virtual double Height(const FVector3d& D, double) const override { return K * R * FVector3d::DotProduct(D, Up); }
        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            Grad = Up * (K * R);
            return Height(D, FootprintCm);
        }
        virtual double OmittedBoundCm(double) const override { return 0.0; }
        virtual double MaxHeightCm() const override { return K * R; }
        virtual double MinHeightCm() const override { return -K * R; }
        virtual double MaxSlope() const override { return K; }

    private:
        double R;
        double K;
        FVector3d Up;
    };

    /** A smooth sphere, its heights bounded +-BoundCm: ground with nothing
     *  on it. A bound of 0 is no ground at all, an ocean's or a giant's. */
    class FSmoothGround final : public IGroundField
    {
    public:
        FSmoothGround(double InRadiusCm, double InBoundCm) : R(InRadiusCm), Bound(InBoundCm) {}
        virtual double RadiusCm() const override { return R; }
        virtual double Height(const FVector3d&, double) const override { return 0.0; }
        virtual double HeightAndGradient(const FVector3d&, FVector3d& Grad, double) const override
        {
            Grad = FVector3d::ZeroVector;
            return 0.0;
        }
        virtual double OmittedBoundCm(double) const override { return 0.0; }
        virtual double MaxHeightCm() const override { return Bound; }
        virtual double MinHeightCm() const override { return -Bound; }
        virtual double MaxSlope() const override { return 0.0; }

    private:
        double R;
        double Bound;
    };

    /** Every height asked of Inner, in order. */
    class FRecordingGround final : public IGroundField
    {
    public:
        struct FRead
        {
            FVector3d D;
            double FootprintCm;
        };
        mutable TArray<FRead> Reads;

        explicit FRecordingGround(const IGroundField& InInner) : Inner(InInner) {}
        virtual double RadiusCm() const override { return Inner.RadiusCm(); }
        virtual double Height(const FVector3d& D, double FootprintCm) const override
        {
            Reads.Add({ D, FootprintCm });
            return Inner.Height(D, FootprintCm);
        }
        virtual double HeightAndGradient(const FVector3d& D, FVector3d& Grad, double FootprintCm) const override
        {
            return Inner.HeightAndGradient(D, Grad, FootprintCm);
        }
        virtual double OmittedBoundCm(double FootprintCm) const override { return Inner.OmittedBoundCm(FootprintCm); }
        virtual double MaxHeightCm() const override { return Inner.MaxHeightCm(); }
        virtual double MinHeightCm() const override { return Inner.MinHeightCm(); }
        virtual double MaxSlope() const override { return Inner.MaxSlope(); }

    private:
        const IGroundField& Inner;
    };

    /** The star Elevation rad above D's level, toward Toward's part along it. */
    FSunLight LightAt(const FVector3d& D, const FVector3d& Toward, double Elevation, double AngularRadius)
    {
        const FVector3d Level = (Toward - D * FVector3d::DotProduct(Toward, D)).GetSafeNormal();
        FSunLight Sun;
        Sun.Direction = (D * FMath::Sin(Elevation) + Level * FMath::Cos(Elevation)).GetSafeNormal();
        Sun.AngularRadius = AngularRadius;
        return Sun;
    }

    /** A seed offset as the sky's are: three multiples of 1/256 under 256. */
    FVector3d OffsetFrom(FRandomStream& Stream)
    {
        const int32 X = Stream.RandRange(0, 65535);
        const int32 Y = Stream.RandRange(0, 65535);
        const int32 Z = Stream.RandRange(0, 65535);
        return FVector3d(X, Y, Z) / 256.0;
    }

    /** The truth the geometric march approximates: the heights at every
     *  Step = Footprint / R toward the star out to the march's end, read at
     *  the footprint, and AlongProfile over them -- the orbit map's own
     *  schedule. */
    FSunVisibility Dense(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm, double Steepest)
    {
        const double R = Ground.RadiusCm();
        const double SinT = FVector3d::DotProduct(Sun.Direction, D);
        const FVector3d Level = Sun.Direction - D * SinT;
        const FVector3d Toward = Level.GetSafeNormal();
        const double Theta = FMath::Atan2(SinT, Level.Size());
        const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunRadiusMax);
        const double Here = Ground.Height(D, FootprintCm);
        const double End = MarchEnd(R, Here, Ground.MaxHeightCm(), Theta - Radius);
        const double Step = FootprintCm / R;
        TArray<double> Heights = { Here };
        for (int32 K = 1; K * Step <= End; ++K)
        {
            const double Arc = K * Step;
            Heights.Add(Ground.Height(D * FMath::Cos(Arc) + Toward * FMath::Sin(Arc), FootprintCm));
        }
        return AlongProfile(R, Ground.MaxHeightCm(), Ground.MinHeightCm(), Steepest, Heights, Step, Theta, Sun.AngularRadius);
    }
}

bool FSunShadowKnownValuesTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // The disc above a straight horizon.
    TestTrue(TEXT("a horizon a radius under the disc's centre leaves it whole"), DiscAbove(-1.0) == 1.0);
    TestTrue(TEXT("one through its centre leaves half"), FMath::IsNearlyEqual(DiscAbove(0.0), 0.5, 1e-15));
    TestTrue(TEXT("one a radius over it hides it"), DiscAbove(1.0) == 0.0);
    TestTrue(TEXT("and past either edge it stays there"), DiscAbove(-7.0) == 1.0 && DiscAbove(7.0) == 0.0);
    // (acos x - x sqrt(1 - x^2)) / pi. The + form is symmetric and falling
    // too, and agrees at -1, 0 and 1; it gives 0.4712 at 0.5.
    TestTrue(FString::Printf(TEXT("half a radius over the centre leaves 0.195501 (%.12f)"), DiscAbove(0.5)),
        FMath::IsNearlyEqual(DiscAbove(0.5), 0.19550110947788538, 1e-12));
    TestTrue(FString::Printf(TEXT("half a radius under it, 0.804499 (%.12f)"), DiscAbove(-0.5)),
        FMath::IsNearlyEqual(DiscAbove(-0.5), 0.8044988905221148, 1e-12));
    TestTrue(FString::Printf(TEXT("a fifth over it, 0.373530 (%.12f)"), DiscAbove(0.2)),
        FMath::IsNearlyEqual(DiscAbove(0.2), 0.373530039052331, 1e-12));

    // The elevation: the triangle of the two points and the centre, exactly.
    TestTrue(FString::Printf(TEXT("level ground dips away by half the arc (%.15f, %.15f)"), Elevation(1.0e8, 0.0, 0.0, 0.01), Elevation(1.0e8, 0.0, 0.0, 0.3)),
        FMath::IsNearlyEqual(Elevation(1.0e8, 0.0, 0.0, 0.01), -0.005, 1e-15) && FMath::IsNearlyEqual(Elevation(1.0e8, 0.0, 0.0, 0.3), -0.15, 1e-15));
    TestTrue(FString::Printf(TEXT("1 km up, 10 km off, over a 1,000 km world stands 0.0946183 rad up (%.15f)"), Elevation(1.0e8, 0.0, 1.0e5, 0.01)),
        FMath::IsNearlyEqual(Elevation(1.0e8, 0.0, 1.0e5, 0.01), 0.0946183473562076, 1e-13));

    // The march's end, acos(rho cos L) - L: here rho is cos 0.1.
    const double R = 1.0e8;
    const double Peak = R * (1.0 / FMath::Cos(0.1) - 1.0);
    TestTrue(FString::Printf(TEXT("with the sun's edge on the level the march ends 0.1 rad off (%.15f)"), MarchEnd(R, 0.0, Peak, 0.0)),
        FMath::IsNearlyEqual(MarchEnd(R, 0.0, Peak, 0.0), 0.1, 1e-12));
    TestTrue(FString::Printf(TEXT("with it 0.05 under, 0.161766 rad off (%.15f)"), MarchEnd(R, 0.0, Peak, -0.05)),
        FMath::IsNearlyEqual(MarchEnd(R, 0.0, Peak, -0.05), 0.16176609378183154, 1e-12));
    TestTrue(TEXT("and there the highest ground stands exactly at the sun's lower edge"),
        FMath::IsNearlyEqual(Elevation(R, 0.0, Peak, MarchEnd(R, 0.0, Peak, -0.05)), -0.05, 1e-12));
    TestTrue(TEXT("from the peak itself, with the edge up, there is nothing to look for"), FMath::Abs(MarchEnd(R, Peak, Peak, 0.3)) <= 1e-12);

    // The night's dip, to the lowest ground's tangent.
    TestTrue(FString::Printf(TEXT("1 km over a 1,000 km world whose lowest ground is 1 km down dips 0.0632245 rad (%.15f)"), NightDip(1.0e8, 1.0e5, -1.0e5)),
        FMath::IsNearlyEqual(NightDip(1.0e8, 1.0e5, -1.0e5), 0.06322448399238306, 1e-13));
    TestTrue(TEXT("and none from the lowest ground itself"), NightDip(1.0e8, -1.0e5, -1.0e5) == 0.0);

    // A plane tilted 5 degrees up toward the star: the horizon is the slope's
    // own, atan K, to the first sample's curve (under 1e-6 rad).
    const double Radius = 0.03;
    const FVector3d D(1.0, 0.0, 0.0);
    const FVector3d Up(0.0, 1.0, 0.0);
    const FTiltedGround Tilted(6.0e8, FMath::Tan(FMath::DegreesToRadians(5.0)), Up);
    const double Slope = FMath::Atan(Tilted.MaxSlope());
    struct FCase { double Over; double Seen; };
    for (const FCase& Case : { FCase{ 0.5, 0.8044988905221148 }, FCase{ 0.0, 0.5 }, FCase{ -0.5, 0.19550110947788538 } })
    {
        const double Theta = Slope + Case.Over * Radius;
        const FSunVisibility Seen = Visible(Tilted, D, LightAt(D, Up, Theta, Radius), 100.0, Tilted.MaxSlope());
        TestTrue(FString::Printf(TEXT("uphill, the sun's centre %.1f radii over the slope shows %.6f of it (%.8f)"), Case.Over, Case.Seen, Seen.Visible),
            FMath::Abs(Seen.Visible - Case.Seen) <= 1.0e-4);
        TestTrue(FString::Printf(TEXT("its horizon is the slope's, atan K (%.3e off)"), Seen.Horizon - Slope), FMath::Abs(Seen.Horizon - Slope) <= 1.0e-6);
        TestTrue(TEXT("and what it sees is the disc above that horizon"), FMath::IsNearlyEqual(Seen.Visible, DiscAbove((Seen.Horizon - Theta) / Radius), 1e-12));
    }
    const FSunVisibility Downhill = Visible(Tilted, D, LightAt(D, -Up, Slope, Radius), 100.0, Tilted.MaxSlope());
    TestTrue(FString::Printf(TEXT("downhill, the same sun is whole (%.8f)"), Downhill.Visible), Downhill.Visible == 1.0);

    // A profile: a wall 64.5 m high three steps (3 km) off over a 1,000 km
    // world, the sun 0.02 rad up and 0.01 in radius: the wall's top is all
    // but on the sun's centre.
    const TArray<double> Wall = { 0.0, 0.0, 0.0, 6450.0, 0.0, 0.0, 0.0, 0.0 };
    const FSunVisibility Profiled = AlongProfile(1.0e8, 1.0e4, -1.0e4, 1.0e3, Wall, 0.001, 0.02, 0.01);
    TestTrue(FString::Printf(TEXT("the wall is the horizon, 0.0199960 rad (%.15f)"), Profiled.Horizon),
        FMath::IsNearlyEqual(Profiled.Horizon, 0.019995978977504523, 1e-13));
    TestTrue(FString::Printf(TEXT("and leaves 0.500256 of the sun (%.12f)"), Profiled.Visible),
        FMath::IsNearlyEqual(Profiled.Visible, 0.5002559862356774, 1e-12));
    TestEqual(TEXT("past it nothing 100 m high could rise over the wall: four steps read"), Profiled.Reads, 4);
    return true;
}

bool FSunShadowExitsTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    const double Radius = 0.03;
    const FVector3d D(1.0, 0.0, 0.0);
    const FVector3d East(0.0, 1.0, 0.0);

    // Smooth ground, 1 cm of relief: the only horizon is the sphere's own.
    const FSmoothGround Smooth(6.0e8, 1.0);
    const FSunVisibility Level = Visible(Smooth, D, LightAt(D, East, 0.0, Radius), 100.0, Smooth.MaxSlope());
    TestTrue(FString::Printf(TEXT("its centre on the level, half the sun shows (%.8f), finite"), Level.Visible),
        FMath::IsFinite(Level.Visible) && FMath::Abs(Level.Visible - 0.5) <= 1.0e-5);
    TestTrue(FString::Printf(TEXT("and the bound ends the march: nothing on smooth ground rises past the first sample (%d reads)"), Level.Reads),
        Level.Reads > 1 && Level.Reads < 1 + DefaultSamples);
    const FSunVisibility Up = Visible(Smooth, D, LightAt(D, East, 2.0 * Radius, Radius), 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("two radii up it is whole, by the day's exit, unread"), Up.Visible == 1.0 && Up.Reads == 0);
    const FSunVisibility Down = Visible(Smooth, D, LightAt(D, East, -2.0 * Radius, Radius), 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("two radii down it is hidden, by the night's, after its own height"), Down.Visible == 0.0 && Down.Reads == 1);
    double Rising = -1.0;
    for (int32 Step = -6; Step <= 6; ++Step)
    {
        const double Seen = Visible(Smooth, D, LightAt(D, East, Step * 0.25 * Radius, Radius), 100.0, Smooth.MaxSlope()).Visible;
        TestTrue(FString::Printf(TEXT("and it rises with the sun (%.2f radii: %.6f)"), Step * 0.25, Seen), Seen >= Rising);
        Rising = Seen;
    }

    // The star overhead and underfoot: no azimuth to march along.
    const FSunLight Zenith{ D, Radius };
    const FSunVisibility Overhead = Visible(Smooth, D, Zenith, 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("the star overhead is whole, finite"), Overhead.Visible == 1.0 && FMath::IsFinite(Overhead.Horizon));
    const FSunLight Nadir{ -D, Radius };
    const FSunVisibility Underfoot = Visible(Smooth, D, Nadir, 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("the star underfoot is hidden, finite"), Underfoot.Visible == 0.0 && FMath::IsFinite(Underfoot.Horizon));

    // A star seen from inside its radius: pi/2, clamped to SunRadiusMax, so
    // a centre 0.1 rad under the level shows DiscAbove(0.2) of it.
    const FSunVisibility Huge = Visible(Smooth, D, LightAt(D, East, -0.1, UE_DOUBLE_HALF_PI), 100.0, Smooth.MaxSlope());
    TestTrue(FString::Printf(TEXT("a pi/2 star 0.1 rad down is clamped to 0.5 rad and shows 0.373530 (%.8f)"), Huge.Visible),
        FMath::Abs(Huge.Visible - 0.373530039052331) <= 1.0e-5);

    // No ground, and no star: nothing is cast and nothing is read.
    const FSmoothGround Sea(6.0e8, 0.0);
    for (const double Degrees : { 2.0, -30.0 })
    {
        const FSunVisibility Wet = Visible(Sea, D, LightAt(D, East, FMath::DegreesToRadians(Degrees), Radius), 100.0, Sea.MaxSlope());
        TestTrue(FString::Printf(TEXT("a world with no ground casts nothing at %.0f degrees"), Degrees), Wet.Visible == 1.0 && Wet.Reads == 0);
    }
    const FSunVisibility Dark = Visible(Smooth, D, FSunLight(), 100.0, Smooth.MaxSlope());
    TestTrue(TEXT("without a star nothing is cast"), Dark.Visible == 1.0 && Dark.Reads == 0);

    // The real ground's exits meet its march with no step: just over the
    // day's the disc is whole unread, just under it the march finds it
    // whole; just under the night's it is hidden unread, just over it the
    // march finds it hidden.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Real(Params);
    const double Steep = SteepestSlope(Params);
    const double Day = FMath::Atan(Steep) + Radius;
    FRandomStream Stream(20260928);
    for (int32 Sample = 0; Sample < 24; ++Sample)
    {
        const FVector3d Here(Stream.GetUnitVector());
        const FVector3d Toward(Stream.GetUnitVector());
        const FSunVisibility Over = Visible(Real, Here, LightAt(Here, Toward, Day + 1.0e-6, Radius), 1.0e4, Steep);
        const FSunVisibility Under = Visible(Real, Here, LightAt(Here, Toward, Day - 0.5 * Radius, Radius), 1.0e4, Steep);
        TestTrue(TEXT("over the day's exit the disc is whole, unread"), Over.Visible == 1.0 && Over.Reads == 0);
        TestTrue(FString::Printf(TEXT("under it the march runs and finds it whole (%.6f, %d reads)"), Under.Visible, Under.Reads),
            Under.Reads > 0 && Under.Visible >= 0.999);
        const double Dip = NightDip(Real.RadiusCm(), Real.Height(Here, 1.0e4), Real.MinHeightCm());
        const FSunVisibility Below = Visible(Real, Here, LightAt(Here, Toward, -Dip - Radius - 1.0e-6, Radius), 1.0e4, Steep);
        const FSunVisibility Above = Visible(Real, Here, LightAt(Here, Toward, -Dip - Radius + 1.0e-6, Radius), 1.0e4, Steep);
        TestTrue(TEXT("under the night's exit the disc is hidden, only the point's height read"), Below.Visible == 0.0 && Below.Reads == 1);
        TestTrue(FString::Printf(TEXT("over it the march runs and finds it hidden (%.6f, %d reads)"), Above.Visible, Above.Reads),
            Above.Reads > 1 && Above.Visible <= 1.0e-3);
    }
    return true;
}

bool FSunShadowScheduleTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // Downhill on the tilted plane nothing hides the sun and nothing ends
    // the march early, so every sample is read: the point's own height at the
    // footprint, then the geometric series from Nearest to MarchEnd, each read
    // at twice the gap ahead of it.
    const double R = 6.0e8;
    const double Footprint = 3.0e3;
    const FVector3d D(1.0, 0.0, 0.0);
    const FVector3d Up(0.0, 1.0, 0.0);
    const FTiltedGround Tilted(R, FMath::Tan(FMath::DegreesToRadians(5.0)), Up);
    const FRecordingGround Recorded(Tilted);
    const FSunLight Sun = LightAt(D, -Up, 0.0, 0.03);
    const FSunVisibility Seen = Visible(Recorded, D, Sun, Footprint, Tilted.MaxSlope(), 12);
    if (!TestEqual(TEXT("the point's own height, then twelve samples"), Recorded.Reads.Num(), 13))
    {
        return false;
    }
    TestEqual(TEXT("and the result counts them"), Seen.Reads, 13);
    TestTrue(TEXT("the first read is the point, at its footprint"), Recorded.Reads[0].D.Equals(D, 1e-15) && Recorded.Reads[0].FootprintCm == Footprint);
    const double End = MarchEnd(R, 0.0, Tilted.MaxHeightCm(), -0.03);
    const double Nearest = Footprint / R;
    const double Growth = FMath::Pow(End / Nearest, 1.0 / 11.0);
    for (int32 Sample = 0; Sample < 12; ++Sample)
    {
        const double Along = Nearest * FMath::Pow(Growth, Sample);
        const FVector3d Want = D * FMath::Cos(Along) - Up * FMath::Sin(Along);
        const double WantFootprint = FMath::Max(Footprint, FootprintFactor * Along * (Growth - 1.0) * R);
        const FRecordingGround::FRead& Read = Recorded.Reads[Sample + 1];
        TestTrue(FString::Printf(TEXT("sample %d lies %.3e rad toward the sun (%.3e off)"), Sample, Along, (Read.D - Want).Size()),
            (Read.D - Want).Size() <= 1e-12);
        TestTrue(FString::Printf(TEXT("and is read at twice the gap ahead, %.6g cm (%.6g)"), WantFootprint, Read.FootprintCm),
            FMath::IsNearlyEqual(Read.FootprintCm, WantFootprint, 1e-9 * WantFootprint));
    }
    return true;
}

bool FSunShadowAgainstProfileTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // The tiles' geometric march against the map's dense one at the same
    // footprint (300 m, a tile's spacing near 30 km up). Planning measured,
    // at 12 samples: the shaded share 0.79-1.03 of the dense one's; mean
    // |dv| 0.04-0.07 at 5 degrees and 0.10-0.14 at 2; at 8 samples 0.07-0.14
    // at 5 -- over this test's bound, so 8 is a quality NO-GO, never a looser
    // bound. Held: the shaded share 0.6-1.2 of the truth's, the penumbra 0.5-2,
    // mean |dv| under 0.10 at 5 degrees and 0.18 at 2.
    FWorldReliefParams Fourth = GroundFixtures::FixtureParams();
    FWorldReliefParams Fifth = GroundFixtures::FixtureParams();
    Fifth.RadiusCm = 0.843852 * 6.3781e8;
    Fifth.PeakCm = 6.12194e5;
    Fifth.Cratering = 0.15;
    struct FCase { const TCHAR* Name; const FWorldReliefParams* Params; double SunRadius; double Degrees; double MeanGap; };
    const FCase Cases[] = {
        { TEXT("IV-like, 5 degrees"), &Fourth, 0.02903, 5.0, 0.10 },
        { TEXT("IV-like, 2 degrees"), &Fourth, 0.02903, 2.0, 0.18 },
        { TEXT("V-like, 5 degrees"), &Fifth, 0.01700, 5.0, 0.10 },
        { TEXT("V-like, 2 degrees"), &Fifth, 0.01700, 2.0, 0.18 },
    };
    constexpr double Footprint = 3.0e4;
    constexpr int32 Count = 100;
    FRandomStream Stream(20260929);
    for (const FCase& Case : Cases)
    {
        int32 MarchShaded = 0;
        int32 TruthShaded = 0;
        int32 MarchPenumbra = 0;
        int32 TruthPenumbra = 0;
        double Gap = 0.0;
        double GapAt[2] = { 0.0, 0.0 };
        for (int32 Sample = 0; Sample < Count; ++Sample)
        {
            FWorldReliefParams Params = *Case.Params;
            Params.SeedOffset = OffsetFrom(Stream);
            const FReliefGround Ground(Params);
            const double Steep = SteepestSlope(Params);
            const FVector3d D(Stream.GetUnitVector());
            const FSunLight Sun = LightAt(D, FVector3d(Stream.GetUnitVector()), FMath::DegreesToRadians(Case.Degrees), Case.SunRadius);
            const double Truth = Dense(Ground, D, Sun, Footprint, Steep).Visible;
            const double March = Visible(Ground, D, Sun, Footprint, Steep).Visible;
            GapAt[0] += FMath::Abs(Visible(Ground, D, Sun, Footprint, Steep, 8).Visible - Truth);
            GapAt[1] += FMath::Abs(Visible(Ground, D, Sun, Footprint, Steep, 16).Visible - Truth);
            MarchShaded += March < 0.5 ? 1 : 0;
            TruthShaded += Truth < 0.5 ? 1 : 0;
            MarchPenumbra += March > 0.01 && March < 0.99 ? 1 : 0;
            TruthPenumbra += Truth > 0.01 && Truth < 0.99 ? 1 : 0;
            Gap += FMath::Abs(March - Truth);
        }
        const double Ratio = TruthShaded > 0 ? static_cast<double>(MarchShaded) / TruthShaded : 0.0;
        const double PenumbraRatio = TruthPenumbra > 0 ? static_cast<double>(MarchPenumbra) / TruthPenumbra : 0.0;
        AddInfo(FString::Printf(TEXT("%s: shaded %d against the dense march's %d (%.2f), penumbra %d against %d, mean |dv| %.3f at %d samples, %.3f at 8, %.3f at 16"),
            Case.Name, MarchShaded, TruthShaded, Ratio, MarchPenumbra, TruthPenumbra, Gap / Count, DefaultSamples, GapAt[0] / Count, GapAt[1] / Count));
        TestTrue(FString::Printf(TEXT("%s: the march shades 0.6 to 1.2 of the dense march's ground (%.2f of %d)"), Case.Name, Ratio, TruthShaded),
            TruthShaded >= 10 && Ratio >= 0.6 && Ratio <= 1.2);
        TestTrue(FString::Printf(TEXT("%s: its penumbra is the sun's, 0.5 to 2 of the dense one's (%.2f)"), Case.Name, PenumbraRatio),
            PenumbraRatio >= 0.5 && PenumbraRatio <= 2.0);
        TestTrue(FString::Printf(TEXT("%s: mean |dv| under %.2f (%.3f)"), Case.Name, Case.MeanGap, Gap / Count), Gap / Count <= Case.MeanGap);
    }
    return true;
}

bool FSunShadowSteepestSlopeTest::RunTest(const FString& Parameters)
{
    using namespace SunShadowTestLocal;
    // The day exit's slopes, re-measured: 128 worlds' offsets and 1,024
    // directions each, from FRandomStream(20260928), of |grad| along the
    // ground at footprint 0 -- the detail bands on a world with twenty of
    // them (8e9 cm: a world needs 6.3e9 cm of radius for a twentieth band and
    // 1.26e10 for a twenty-first, and no rocky world is within a factor of five
    // of either), and all six crater bands. Planning measured 16.72 and 2.30
    // with another generator. The constants times the margin must be 1.3 to
    // 1.8 times what this run measures: steeper than anything sampled, and
    // not so steep the day side stops exiting.
    FWorldReliefParams Big;
    Big.RadiusCm = 8.0e9;
    Big.PeakCm = 1.0e5;
    Big.Cratering = 1.0;
    Big.Ground = EGround::Solid;
    TestEqual(TEXT("the sampled world carries twenty detail bands"), FWorldRelief(Big).GetDetailFrequencies().Num(), 20);
    FRandomStream Stream(20260928);
    double Detail = 0.0;
    double Craters = 0.0;
    for (int32 World = 0; World < 128; ++World)
    {
        Big.SeedOffset = OffsetFrom(Stream);
        const FWorldRelief Relief(Big);
        for (int32 Sample = 0; Sample < 1024; ++Sample)
        {
            const FVector3d D(Stream.GetUnitVector());
            FVector3d G;
            Relief.DetailSum(D, 0.0, &G);
            Detail = FMath::Max(Detail, (G - D * FVector3d::DotProduct(G, D)).Size());
            Relief.CraterSum(D, 0.0, &G);
            Craters = FMath::Max(Craters, (G - D * FVector3d::DotProduct(G, D)).Size());
        }
    }
    AddInfo(FString::Printf(TEXT("steepest |grad S| along the ground: detail %.3f, craters %.3f"), Detail, Craters));
    TestTrue(FString::Printf(TEXT("the detail's margin, %.2f, is 1.3 to 1.8 times the steepest sampled (%.3f)"), SteepestMargin * DetailGradientSampled, Detail),
        SteepestMargin * DetailGradientSampled >= 1.3 * Detail && SteepestMargin * DetailGradientSampled <= 1.8 * Detail);
    TestTrue(FString::Printf(TEXT("the craters', %.2f, likewise (%.3f)"), SteepestMargin * CraterGradientSampled, Craters),
        SteepestMargin * CraterGradientSampled >= 1.3 * Craters && SteepestMargin * CraterGradientSampled <= 1.8 * Craters);
    // And the real ground's slope never passes it: the fixture's steepest
    // finite difference between two footprints' heights.
    const FWorldReliefParams Params = GroundFixtures::FixtureParams();
    const FReliefGround Real(Params);
    double Read = 0.0;
    for (int32 Sample = 0; Sample < 2048; ++Sample)
    {
        const FVector3d D(Stream.GetUnitVector());
        const FVector3d Along = FVector3d(Stream.GetUnitVector()).Cross(D).GetSafeNormal();
        constexpr double Gap = 1.0e4;
        const FVector3d There = (D + Along * (Gap / Real.RadiusCm())).GetSafeNormal();
        Read = FMath::Max(Read, FMath::Abs(Real.Height(There, 2.0 * Gap) - Real.Height(D, Gap)) / Gap);
    }
    TestTrue(FString::Printf(TEXT("the fixture's steepest read slope (%.4f) is under the exit's (%.4f)"), Read, SteepestSlope(Params)), Read < SteepestSlope(Params));
    return true;
}

#endif
