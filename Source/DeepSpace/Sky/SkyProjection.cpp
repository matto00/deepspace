#include "Sky/SkyProjection.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** The first star, the one every planet is lit from. One per system in
     *  the POC; a binary is the same code with a sum where this is used. */
    int32 FindStar(const FSkySystem& System)
    {
        return System.Bodies.IndexOfByPredicate([](const FSkyBody& Body) { return Body.Kind == ESkyBodyKind::Star; });
    }

    /** Irradiance from Star at a point Distance away, relative to a Sun at
     *  1 AU: the inverse-square law, uncompressed. */
    double IrradianceRatio(const FSkyBody& Star, double DistanceCm)
    {
        const double AU = DistanceCm / UniverseUnits::CmPerAU;
        return AU > 0.0 ? Star.Luminosity / (AU * AU) : 0.0;
    }

    /** The per-body quantities that fix a proxy before stacking. */
    struct FBodyShape
    {
        double RenderDistance = 0.0;    // true, or clamped to the altitude floor
        double Sin = 0.0;               // sine of the drawn angular radius
        double NearFactor = 1.0;        // proxy near side over proxy centre distance
        double FarFactor = 1.0;         // proxy far side over proxy centre distance
        double Power = 0.0;             // d^2 - R^2 at the rendered distance
    };
}

double SkyProjection::LambertPhase(double Alpha)
{
    const double A = FMath::Clamp(Alpha, 0.0, UE_DOUBLE_PI);
    return (FMath::Sin(A) + (UE_DOUBLE_PI - A) * FMath::Cos(A)) / UE_DOUBLE_PI;
}

double SkyProjection::DiscOverlapFraction(double Separation, double RadiusA, double RadiusB)
{
    const double S = FMath::Abs(Separation);
    const double A = FMath::Max(RadiusA, 0.0);
    const double B = FMath::Max(RadiusB, 0.0);
    if (A <= 0.0)
    {
        // A disc with no area: covered exactly when its centre is.
        return S < B ? 1.0 : 0.0;
    }
    if (B <= 0.0 || S >= A + B)
    {
        return 0.0;
    }
    if (S <= B - A)
    {
        return 1.0;
    }
    if (S <= A - B)
    {
        return (B * B) / (A * A);
    }

    // The lens is two circular segments either side of the common chord: X
    // and Y are the chord's distances from each centre, H its half-length.
    // Each is written as products of sums and differences of the radii and
    // the separation, so a small sun at a large planet's limb, where S and B
    // agree to many digits, does not lose them all to one subtraction; and
    // the angles are atan2 of the chord, not arc-cosines, which near a
    // grazing contact have no digits left.
    const double X = ((S - B) * (S + B) + A * A) / (2.0 * S);
    const double Y = ((S - A) * (S + A) + B * B) / (2.0 * S);
    const double H = FMath::Sqrt(FMath::Max((A + B - S) * (S - A + B) * (S + A - B) * (S + A + B), 0.0)) / (2.0 * S);
    const double Lens = A * A * FMath::Atan2(H, X) + B * B * FMath::Atan2(H, Y) - S * H;
    return FMath::Clamp(Lens / (UE_DOUBLE_PI * A * A), 0.0, 1.0);
}

double SkyProjection::Compress(double Ratio, double Gamma)
{
    return Ratio > 0.0 ? FMath::Pow(Ratio, Gamma) : 0.0;
}

double SkyProjection::ConeSolidAngle(double AngularRadius)
{
    const double Half = FMath::Sin(0.5 * AngularRadius);
    return 4.0 * UE_DOUBLE_PI * Half * Half;
}

FSkyFrame SkyProjection::Project(const FSkySystem& System, const FUniversePosition& Ship, const FSkyViewParams& Params)
{
    FSkyFrame Frame;
    const int32 Count = System.Bodies.Num();
    Frame.Bodies.SetNum(Count);

    const int32 StarIndex = FindStar(System);
    const FSkyBody* Star = StarIndex != INDEX_NONE ? &System.Bodies[StarIndex] : nullptr;

    // A point is drawn this big, and no smaller: two pixels across.
    const double MinDrawnRadius = 0.5 * Params.MinPointPixels * Params.PixelAngle;

    TArray<FBodyShape> Shapes;
    Shapes.SetNum(Count);

    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FSkyBody& Body = System.Bodies[Index];
        FSkyBodyView& View = Frame.Bodies[Index];
        FBodyShape& Shape = Shapes[Index];

        // Through the chunk index, never the offsets (ADR 0007).
        const FVector Delta = Body.Position - Ship;
        const double Distance = Delta.Size();
        View.Distance = Distance;
        View.Direction = Distance > 0.0 ? Delta / Distance : FVector::ForwardVector;
        View.AngularRadius = Distance > Body.Radius ? FMath::Asin(Body.Radius / Distance) : 0.5 * UE_DOUBLE_PI;

        // Below the floor the body stops growing rather than engulfing the
        // view; the direction is still the true one.
        const double Floor = FMath::Max(Params.MinRenderedAltitude, Params.MinRenderedAltitudeOfRadius * Body.Radius);
        Shape.RenderDistance = FMath::Max(Distance, Body.Radius + Floor);
        const double RenderSin = Body.Radius / Shape.RenderDistance;
        const double RenderRadius = FMath::Asin(RenderSin);
        Shape.Power = Shape.RenderDistance * Shape.RenderDistance - Body.Radius * Body.Radius;

        const bool bInflated = RenderRadius < MinDrawnRadius;
        View.DrawnAngularRadius = bInflated ? MinDrawnRadius : RenderRadius;
        Shape.Sin = bInflated ? FMath::Sin(MinDrawnRadius) : RenderSin;
        // (d - R) / d rather than 1 - sin: at 10 km over a planet the sine is
        // 0.9984, and the difference is where the proxy's near side is.
        Shape.NearFactor = bInflated ? 1.0 - Shape.Sin : (Shape.RenderDistance - Body.Radius) / Shape.RenderDistance;
        Shape.FarFactor = 1.0 + Shape.Sin;

        // The resolve: a point below MinPointPixels, a disc above the band,
        // smoothstep between so the blend has no visible start or end.
        const double Pixels = 2.0 * RenderRadius / Params.PixelAngle;
        const double Band = FMath::Max(Params.ResolveBandPixels, UE_DOUBLE_SMALL_NUMBER);
        const double T = FMath::Clamp((Pixels - Params.MinPointPixels) / Band, 0.0, 1.0);
        View.PointBlend = 1.0 - T * T * (3.0 - 2.0 * T);

        // Photometry (sky decision 2). Surface brightness depends on the body
        // and its star, never on the ship.
        if (Body.Kind == ESkyBodyKind::Star)
        {
            const double Warmth = Body.TemperatureK / UniverseUnits::SolarTemperatureK;
            View.SurfaceBrightness = Params.StarSurface * Compress(Warmth * Warmth * Warmth * Warmth, Params.FluxGamma);
            View.Phase = 1.0;
        }
        else if (Star)
        {
            const FVector ToStar = Star->Position - Body.Position;
            const double StarDistance = ToStar.Size();
            View.LightDirection = StarDistance > 0.0 ? ToStar / StarDistance : FVector::ZeroVector;
            View.SurfaceBrightness = Body.Albedo * Compress(IrradianceRatio(*Star, StarDistance), Params.FluxGamma);

            // Phase angle: star and ship, as seen from the body.
            const FVector ToShip = -View.Direction;
            const double CosAlpha = FMath::Clamp(FVector::DotProduct(View.LightDirection, ToShip), -1.0, 1.0);
            View.Phase = LambertPhase(FMath::Acos(CosAlpha));
        }

        if (bInflated)
        {
            // Spread over more pixels than it covers, so dimmer per pixel,
            // but boosted: the flux falls as 1/d^(2 gamma), not 1/d^2. The
            // one lie, told only where the drawn size is already one.
            const double Ratio = ConeSolidAngle(RenderRadius) / ConeSolidAngle(MinDrawnRadius);
            View.PointBoost = Ratio > 0.0 ? FMath::Pow(1.0 / Ratio, 1.0 - Params.FluxGamma) : 1.0;
            View.Brightness = View.SurfaceBrightness * View.Phase * Compress(Ratio, Params.FluxGamma);
        }
        else if (View.PointBlend <= 0.0)
        {
            // Resolved: exactly the surface, whatever the distance.
            View.Brightness = View.SurfaceBrightness;
        }
        else
        {
            // The material draws Brightness * lerp(shaded, 1, PointBlend),
            // and the shaded term averages to Phase over the disc. Divide by
            // what the blend averages to and the total flux is the honest
            // SurfaceBrightness * Phase * solid angle all through the band.
            const double Averaged = View.PointBlend + (1.0 - View.PointBlend) * View.Phase;
            View.Brightness = View.SurfaceBrightness * View.Phase / Averaged;
        }
    }

    // Stack by the power of the ship with respect to each sphere, the squared
    // tangent length: along any ray it is the product of the two hit
    // distances, so it orders disjoint spheres the way a ray meets them. The
    // near-surface distance d - R does not -- it hides a moon in front of a
    // planet whenever the moon is nearer than the planet's limb.
    Frame.DepthOrder.Reserve(Count);
    for (int32 Index = 0; Index < Count; ++Index)
    {
        Frame.DepthOrder.Add(Index);
    }
    Frame.DepthOrder.StableSort([&Shapes](int32 A, int32 B) { return Shapes[A].Power < Shapes[B].Power; });

    // Because the scale is invisible it is reassigned every frame; bodies
    // that swap order in flight move nothing on screen.
    double Cursor = Params.NearProxy;
    for (const int32 Index : Frame.DepthOrder)
    {
        const FBodyShape& Shape = Shapes[Index];
        FSkyBodyView& View = Frame.Bodies[Index];
        const double Centre = Cursor / Shape.NearFactor;
        View.ProxyLocation = View.Direction * Centre;
        View.ProxyRadius = Centre * Shape.Sin;
        Cursor = Centre * Shape.FarFactor * Params.StackGap;
    }

    if (Star)
    {
        const FVector ToStar = Star->Position - Ship;
        const double Distance = ToStar.Size();
        Frame.SunDirection = Distance > 0.0 ? ToStar / Distance : FVector::ZeroVector;
        Frame.SunIrradiance = Compress(IrradianceRatio(*Star, Distance), Params.FluxGamma);

        // The eclipse: whatever is stacked in front of the star, by the same
        // order the picture is drawn in, takes its share of the disc, at the
        // true angular sizes -- a planet inflated to a two-pixel point hides
        // no more sun than it really does. Shares are summed, which counts
        // twice the rare sliver where a moon and its planet both cross the
        // star at once: the deck goes a shade darker than it should for
        // the seconds that lasts, and never lighter.
        const FSkyBodyView& StarView = Frame.Bodies[StarIndex];
        double Covered = 0.0;
        for (const int32 Index : Frame.DepthOrder)
        {
            if (Index == StarIndex)
            {
                break;
            }
            const FSkyBodyView& View = Frame.Bodies[Index];
            const double Separation = FMath::Atan2(FVector::CrossProduct(View.Direction, StarView.Direction).Size(),
                                                   FVector::DotProduct(View.Direction, StarView.Direction));
            Covered += DiscOverlapFraction(Separation, StarView.AngularRadius, View.AngularRadius);
        }
        Frame.SunVisibleFraction = FMath::Clamp(1.0 - Covered, 0.0, 1.0);
    }
    return Frame;
}
