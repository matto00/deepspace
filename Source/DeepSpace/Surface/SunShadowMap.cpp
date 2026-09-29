#include "Surface/SunShadowMap.h"

#include "Surface/GroundField.h"
#include "Surface/WorldRelief.h"

int64 FSunShadowMap::Bytes() const
{
    int64 Total = 0;
    for (const TArray<uint16>& Level : Levels)
    {
        Total += static_cast<int64>(Level.Num()) * sizeof(uint16);
    }
    return Total;
}

FSunShadowMap SunShadowMap::Shape(const IGroundField& Ground, const SunShadow::FSunLight& Sun, int32 Width)
{
    FSunShadowMap Map;
    if (!Sun.IsSet() || Width < 2 || !(Ground.RadiusCm() > 0.0) || !(Ground.MaxHeightCm() > Ground.MinHeightCm()))
    {
        return Map;
    }
    Map.Width = Width;
    Map.Step = 2.0 * UE_DOUBLE_PI / Width;
    Map.FrameZ = Sun.Direction.GetSafeNormal();
    const FVector3d Seed = FMath::Abs(Map.FrameZ.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX();
    Map.FrameX = FVector3d::CrossProduct(Seed, Map.FrameZ).GetSafeNormal();
    Map.FrameY = FVector3d::CrossProduct(Map.FrameZ, Map.FrameX);
    const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunShadow::SunRadiusMax);
    Map.PsiLo = -(SunShadow::NightDip(Ground.RadiusCm(), Ground.MaxHeightCm(), Ground.MinHeightCm()) + Radius);
    Map.Rows = RowsFor(Width, Map.PsiLo);
    return Map;
}

int32 SunShadowMap::RowsFor(int32 Width, double PsiLo)
{
    return Width > 0 ? FMath::CeilToInt32((0.5 * UE_DOUBLE_PI - PsiLo) / (2.0 * UE_DOUBLE_PI / Width)) : 0;
}

FVector3d SunShadowMap::TexelDirection(const FSunShadowMap& Map, int32 Column, int32 Row)
{
    const double Azimuth = -UE_DOUBLE_PI + (Column + 0.5) * Map.Step;
    const double Elevation = Map.PsiLo + (Row + 0.5) * Map.Step;
    return (Map.FrameX * FMath::Cos(Azimuth) + Map.FrameY * FMath::Sin(Azimuth)) * FMath::Cos(Elevation) + Map.FrameZ * FMath::Sin(Elevation);
}

uint16 SunShadowMap::Quantise(double Visible)
{
    return static_cast<uint16>(FMath::Clamp(FMath::RoundToInt32(Visible * 65535.0), 0, 65535));
}

FSunShadowMap SunShadowMap::Bake(const IGroundField& Ground, const SunShadow::FSunLight& Sun, double SteepestSlope, int32 Width,
                                 const std::atomic<bool>* Cancel)
{
    FSunShadowMap Map = Shape(Ground, Sun, Width);
    if (Map.Width == 0)
    {
        return Map;
    }
    const double R = Ground.RadiusCm();
    const double Peak = Ground.MaxHeightCm();
    const double Lowest = Ground.MinHeightCm();
    const double FootprintCm = Map.Step * R;
    const double Radius = FMath::Clamp(Sun.AngularRadius, 1.0e-6, SunShadow::SunRadiusMax);
    const double DayExit = FMath::Atan(SteepestSlope) + Radius;
    Map.Levels.SetNum(1);
    Map.Levels[0].SetNumUninitialized(Map.Width * Map.Rows);
    TArray<double> Heights;
    Heights.SetNumUninitialized(Map.Rows);
    TBitArray<> Read;
    for (int32 Column = 0; Column < Map.Width; ++Column)
    {
        if (Cancel && Cancel->load(std::memory_order_relaxed))
        {
            return FSunShadowMap();
        }
        Read.Init(false, Map.Rows);
        const auto HeightAt = [&](int32 Row)
        {
            if (!Read[Row])
            {
                Heights[Row] = Ground.Height(TexelDirection(Map, Column, Row), FootprintCm);
                Read[Row] = true;
            }
            return Heights[Row];
        };
        for (int32 Row = 0; Row < Map.Rows; ++Row)
        {
            const double Psi = Map.PsiLo + (Row + 0.5) * Map.Step;
            uint16& Texel = Map.Levels[0][Row * Map.Width + Column];
            if (Psi >= DayExit)
            {
                Texel = 65535;
                continue;
            }
            const double Here = HeightAt(Row);
            if (Psi + Radius <= -SunShadow::NightDip(R, Here, Lowest))
            {
                Texel = 0;
                continue;
            }
            // The rows ahead the march can reach: every one whose arc is
            // within MarchEnd, as AlongProfile counts them.
            const double End = SunShadow::MarchEnd(R, Here, Peak, Psi - Radius);
            int32 Count = 1;
            while (Row + Count < Map.Rows && Count * Map.Step <= End)
            {
                HeightAt(Row + Count);
                ++Count;
            }
            Texel = Quantise(SunShadow::AlongProfile(R, Peak, Lowest, SteepestSlope, MakeArrayView(Heights.GetData() + Row, Count),
                Map.Step, Psi, Sun.AngularRadius).Visible);
        }
    }
    BuildLevels(Map);
    return Map;
}

void SunShadowMap::BuildLevels(FSunShadowMap& Map)
{
    if (Map.Levels.Num() == 0)
    {
        return;
    }
    Map.Levels.SetNum(1);
    for (int32 Level = 0; Map.WidthAt(Level) > 1 || Map.RowsAt(Level) > 1; ++Level)
    {
        const int32 W = Map.WidthAt(Level);
        const int32 H = Map.RowsAt(Level);
        const int32 NextW = Map.WidthAt(Level + 1);
        const int32 NextH = Map.RowsAt(Level + 1);
        TArray<uint16> Next;
        Next.SetNumUninitialized(NextW * NextH);
        const TArray<uint16>& Here = Map.Levels[Level];
        for (int32 Y = 0; Y < NextH; ++Y)
        {
            const int32 Y0 = FMath::Min(2 * Y, H - 1);
            const int32 Y1 = FMath::Min(2 * Y + 1, H - 1);
            for (int32 X = 0; X < NextW; ++X)
            {
                const int32 X0 = FMath::Min(2 * X, W - 1);
                const int32 X1 = FMath::Min(2 * X + 1, W - 1);
                const uint32 Sum = static_cast<uint32>(Here[Y0 * W + X0]) + Here[Y0 * W + X1] + Here[Y1 * W + X0] + Here[Y1 * W + X1];
                Next[Y * NextW + X] = static_cast<uint16>((Sum + 2) / 4);
            }
        }
        Map.Levels.Add(MoveTemp(Next));
    }
}

double SunShadowMap::Sample(const FSunShadowMap& Map, const FVector3d& D, double Footprint)
{
    if (Map.LevelCount() == 0)
    {
        return 1.0;
    }
    const WorldReliefNoise::FShadowCoord C = WorldReliefNoise::ShadowMapCoordF64(D, Map.FrameX, Map.FrameZ, Map.PsiLo, Map.Step, Footprint, Map.LevelCount());
    if (C.Night != 0)
    {
        return 0.0;   // under PsiLo: provably dark
    }
    double Seen[2];
    for (int32 Pick = 0; Pick < 2; ++Pick)
    {
        const int32 Level = Pick == 0 ? C.Level0 : C.Level1;
        const WorldReliefNoise::FShadowTaps T = WorldReliefNoise::ShadowTapsF64(C.U, C.V, Map.Width, Map.Rows, Level);
        const auto At = [&](int32 X, int32 Y) { return Map.Texel(Level, X, Y) / 65535.0; };
        Seen[Pick] = WorldReliefNoise::BilinearF64(At(T.X0, T.Y0), At(T.X1, T.Y0), At(T.X0, T.Y1), At(T.X1, T.Y1), T.FX, T.FY);
    }
    return WorldReliefNoise::LerpF64(Seen[0], Seen[1], C.Blend);
}

float SunShadowMap::SampleF32(const FSunShadowMap& Map, const FVector3f& D, float Footprint)
{
    if (Map.LevelCount() == 0)
    {
        return 1.0f;
    }
    const WorldReliefNoise::FShadowCoord C = WorldReliefNoise::ShadowMapCoordF32(D, FVector3f(Map.FrameX), FVector3f(Map.FrameZ),
        static_cast<float>(Map.PsiLo), static_cast<float>(Map.Step), Footprint, Map.LevelCount());
    if (C.Night != 0)
    {
        return 0.0f;
    }
    float Seen[2];
    for (int32 Pick = 0; Pick < 2; ++Pick)
    {
        const int32 Level = Pick == 0 ? C.Level0 : C.Level1;
        const WorldReliefNoise::FShadowTaps T = WorldReliefNoise::ShadowTapsF32(static_cast<float>(C.U), static_cast<float>(C.V), Map.Width, Map.Rows, Level);
        const auto At = [&](int32 X, int32 Y) { return static_cast<float>(Map.Texel(Level, X, Y)) / 65535.0f; };
        Seen[Pick] = WorldReliefNoise::BilinearF32(At(T.X0, T.Y0), At(T.X1, T.Y0), At(T.X0, T.Y1), At(T.X1, T.Y1),
            static_cast<float>(T.FX), static_cast<float>(T.FY));
    }
    return WorldReliefNoise::LerpF32(Seen[0], Seen[1], static_cast<float>(C.Blend));
}
