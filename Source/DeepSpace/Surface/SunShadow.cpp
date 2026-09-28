#include "Surface/SunShadow.h"

#include "Surface/GroundField.h"
#include "Surface/WorldRelief.h"

double SunShadow::DiscAbove(double X)
{
    const double C = FMath::Clamp(X, -1.0, 1.0);
    return (FMath::Acos(C) - C * FMath::Sqrt(FMath::Max(0.0, 1.0 - C * C))) / UE_DOUBLE_PI;
}

double SunShadow::Elevation(double RadiusCm, double HereCm, double ThereCm, double Arc)
{
    // (R + There) cos a - (R + Here), without cancelling two numbers near R:
    // (There - Here) - 2 (R + There) sin^2(a / 2).
    const double Half = FMath::Sin(0.5 * Arc);
    return FMath::Atan2((ThereCm - HereCm) - 2.0 * (RadiusCm + ThereCm) * Half * Half, (RadiusCm + ThereCm) * FMath::Sin(Arc));
}

double SunShadow::MarchEnd(double RadiusCm, double HereCm, double PeakCm, double Lower)
{
    if (Lower >= UE_DOUBLE_HALF_PI)
    {
        return 0.0;
    }
    const double Rho = (RadiusCm + HereCm) / (RadiusCm + PeakCm);
    return FMath::Max(0.0, FMath::Acos(FMath::Clamp(Rho * FMath::Cos(Lower), -1.0, 1.0)) - Lower);
}

double SunShadow::NightDip(double RadiusCm, double HereCm, double LowestCm)
{
    return HereCm > LowestCm ? FMath::Acos(FMath::Clamp((RadiusCm + LowestCm) / (RadiusCm + HereCm), -1.0, 1.0)) : 0.0;
}

SunShadow::FSunVisibility SunShadow::Visible(const IGroundField& Ground, const FVector3d& D, const FSunLight& Sun, double FootprintCm,
                                             double SteepestSlope, int32 Samples, TOptional<double> HereCm)
{
    FSunVisibility Out;
    const double R = Ground.RadiusCm();
    const double Peak = Ground.MaxHeightCm();
    if (!Sun.IsSet() || !(R > 0.0) || !(Peak > Ground.MinHeightCm()))
    {
        // No star, or no ground to cast with: an ocean's, a giant's.
        return Out;
    }
    const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunRadiusMax);
    const FVector3d L = Sun.Direction.GetSafeNormal();
    const double SinT = FVector3d::DotProduct(L, D);
    const FVector3d Level = L - D * SinT;
    const double CosT = Level.Size();
    const double Theta = FMath::Atan2(SinT, CosT);
    // The day: over the steepest ground there is, the disc is whole.
    const double DayExit = FMath::Atan(SteepestSlope) + Radius;
    if (Theta >= DayExit)
    {
        return Out;
    }
    const double Here = HereCm.IsSet() ? HereCm.GetValue() : Ground.Height(D, FootprintCm);
    Out.Reads = HereCm.IsSet() ? 0 : 1;
    // The night: under every horizon the ground can make, the disc is hidden.
    if (Theta + Radius <= -NightDip(R, Here, Ground.MinHeightCm()))
    {
        Out.Visible = 0.0;
        return Out;
    }
    if (CosT < 1.0e-12)
    {
        // The star straight up, under a steepest slope past vertical: no
        // azimuth, and nothing above.
        return Out;
    }
    const FVector3d Toward = Level / CosT;
    const double End = MarchEnd(R, Here, Peak, Theta - Radius);
    const double Nearest = FMath::Max(FootprintCm, NearestCm) / R;
    if (!(End > Nearest))
    {
        return Out;
    }
    const int32 Count = FMath::Max(Samples, 2);
    const double Growth = FMath::Pow(End / Nearest, 1.0 / (Count - 1));
    double Along = Nearest;
    for (int32 Sample = 0; Sample < Count; ++Sample)
    {
        // No ground from here on -- none above the peak, all of it curving
        // away -- can rise over what is already found.
        if (Elevation(R, Here, Peak, Along) <= Out.Horizon)
        {
            break;
        }
        const double SeenCm = FMath::Max(FootprintCm, FootprintFactor * Along * (Growth - 1.0) * R);
        const FVector3d There = D * FMath::Cos(Along) + Toward * FMath::Sin(Along);
        Out.Horizon = FMath::Max(Out.Horizon, Elevation(R, Here, Ground.Height(There, SeenCm), Along));
        ++Out.Reads;
        // The disc is already hidden.
        if (Out.Horizon >= Theta + Radius)
        {
            break;
        }
        Along *= Growth;
    }
    Out.Visible = DiscAbove((Out.Horizon - Theta) / Radius);
    return Out;
}

SunShadow::FSunVisibility SunShadow::AlongProfile(double RadiusCm, double PeakCm, double LowestCm, double SteepestSlope,
                                                  TConstArrayView<double> Heights, double Step, double SunElevation, double SunRadius)
{
    FSunVisibility Out;
    if (Heights.Num() == 0 || !(RadiusCm > 0.0) || !(PeakCm > LowestCm) || !(Step > 0.0))
    {
        return Out;
    }
    const double Radius = FMath::Clamp(SunRadius, 1.0e-6, SunRadiusMax);
    const double Theta = SunElevation;
    if (Theta - Radius >= FMath::Atan(SteepestSlope))
    {
        return Out;
    }
    const double Here = Heights[0];
    if (Theta + Radius <= -NightDip(RadiusCm, Here, LowestCm))
    {
        Out.Visible = 0.0;
        return Out;
    }
    const double End = MarchEnd(RadiusCm, Here, PeakCm, Theta - Radius);
    for (int32 K = 1; K < Heights.Num() && K * Step <= End; ++K)
    {
        const double Arc = K * Step;
        if (Elevation(RadiusCm, Here, PeakCm, Arc) <= Out.Horizon)
        {
            break;
        }
        Out.Horizon = FMath::Max(Out.Horizon, Elevation(RadiusCm, Here, Heights[K], Arc));
        ++Out.Reads;
        if (Out.Horizon >= Theta + Radius)
        {
            break;
        }
    }
    Out.Visible = DiscAbove((Out.Horizon - Theta) / Radius);
    return Out;
}

double SunShadow::SteepestSlope(const FWorldReliefParams& Params)
{
    return FWorldRelief(Params).SlopeScale() * SteepestMargin
        * (DetailGradientSampled + FMath::Max(Params.Cratering, 0.0) * CraterGradientSampled);
}
