#include "UI/NavText.h"

#include "Universe/SystemDescription.h"
#include "Universe/UniverseUnits.h"

namespace
{
    /** Whole degrees: a pilot steers by the degree, and a decimal would read
     *  as precision the helm does not need. */
    int32 WholeDegrees(double Radians)
    {
        return FMath::RoundToInt32(FMath::Abs(FMath::RadiansToDegrees(Radians)));
    }

    /** Off the axis by more than rounding would show: half a degree. */
    bool Leans(double Component)
    {
        return FMath::Abs(Component) > FMath::Sin(FMath::DegreesToRadians(0.5));
    }

    /** Where a tenth of a degree stops being worth printing: 10 degrees, in
     *  tenths. Past it a disc is far off the nose, and the turn onto it is
     *  made by the whole degree. */
    constexpr int64 TenthsUpTo = 100;

    /** Degrees to aim with: "3.0°" under ten, "12°" above, the unit chosen
     *  on the rounded value. Empty for an angle that rounds to nothing. */
    FString AimDegrees(double Radians)
    {
        const double Degrees = FMath::Abs(FMath::RadiansToDegrees(Radians));
        const int64 Tenths = FMath::RoundToInt64(Degrees * 10.0);
        if (Tenths == 0)
        {
            return FString();
        }
        if (Tenths < TenthsUpTo)
        {
            return FString::Printf(TEXT("%lld.%lld°"), static_cast<long long>(Tenths / 10), static_cast<long long>(Tenths % 10));
        }
        return FString::Printf(TEXT("%lld°"), static_cast<long long>(FMath::RoundToInt64(Degrees)));
    }

    // Duration's boundaries, each on the value its unit would print.
    constexpr int64 SecondsUpTo = 100;
    constexpr int64 MinutesUpTo = 60;
    constexpr int64 TenthsOfAnHourUpTo = 480;   // two days
    constexpr double SecondsPerMinute = 60.0;
    constexpr double SecondsPerTenthOfAnHour = 360.0;
    constexpr double SecondsPerDay = 86400.0;
}

FString NavText::Bearing(const FVector& ShipLocalDir, double ConeRadians)
{
    const FVector Dir = ShipLocalDir.GetSafeNormal();
    if (Dir.IsZero() || ShipNav::OffBoresight(Dir) <= ConeRadians)
    {
        return TEXT("dead ahead");
    }

    const TCHAR* const Side = Dir.Y < 0.0 ? TEXT("to port") : TEXT("to starboard");
    const TCHAR* const Height = Dir.Z < 0.0 ? TEXT("down") : TEXT("up");

    TArray<FString> Parts;
    if (Dir.X < 0.0)
    {
        Parts.Add(TEXT("astern"));
        if (Leans(Dir.Y))
        {
            Parts.Add(Side);
        }
        if (Leans(Dir.Z))
        {
            Parts.Add(Height);
        }
        return FString::Join(Parts, TEXT(", "));
    }

    // Yaw in the ship's horizontal plane, then elevation above it: the two
    // turns a pilot would make, in the order they would make them.
    const int32 Across = WholeDegrees(FMath::Atan2(Dir.Y, Dir.X));
    const int32 Upward = WholeDegrees(FMath::Atan2(Dir.Z, FMath::Sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y)));
    if (Across > 0)
    {
        Parts.Add(FString::Printf(TEXT("%d° %s"), Across, Side));
    }
    if (Upward > 0)
    {
        Parts.Add(FString::Printf(TEXT("%d° %s"), Upward, Height));
    }
    // Outside a cone narrower than a degree, but rounding to nothing either
    // way: close enough to say so.
    return Parts.IsEmpty() ? FString(TEXT("dead ahead")) : FString::Join(Parts, TEXT(", "));
}

FString NavText::TargetBearing(const FVector& ShipLocalDir, double AheadRadians)
{
    const FVector Dir = ShipLocalDir.GetSafeNormal();
    if (Dir.IsZero() || ShipNav::OffBoresight(Dir) <= AheadRadians)
    {
        return TEXT("dead ahead");
    }
    // Astern, degrees are no more help for a world than for a star, and the
    // words are the jump's: this far off the nose it is not inside any disc,
    // so Bearing will not call it dead ahead either.
    if (Dir.X < 0.0)
    {
        return Bearing(Dir, AheadRadians);
    }

    const TCHAR* const Side = Dir.Y < 0.0 ? TEXT("to port") : TEXT("to starboard");
    const TCHAR* const Height = Dir.Z < 0.0 ? TEXT("down") : TEXT("up");

    // The same two turns as Bearing, yaw then elevation, in finer degrees.
    TArray<FString> Parts;
    const FString Across = AimDegrees(FMath::Atan2(Dir.Y, Dir.X));
    const FString Upward = AimDegrees(FMath::Atan2(Dir.Z, FMath::Sqrt(Dir.X * Dir.X + Dir.Y * Dir.Y)));
    if (!Across.IsEmpty())
    {
        Parts.Add(Across + TEXT(" ") + Side);
    }
    if (!Upward.IsEmpty())
    {
        Parts.Add(Upward + TEXT(" ") + Height);
    }
    // Off the disc, but by less than a twentieth of a degree either way,
    // which only an AheadRadians under the floor can arrange.
    return Parts.IsEmpty() ? FString(TEXT("dead ahead")) : FString::Join(Parts, TEXT(", "));
}

FString NavText::Duration(double Seconds)
{
    if (!FMath::IsFinite(Seconds))
    {
        return FString();
    }
    const double Time = FMath::Max(Seconds, 0.0);
    const int64 Whole = FMath::RoundToInt64(Time);
    if (Whole < SecondsUpTo)
    {
        return FString::Printf(TEXT("%lld S"), static_cast<long long>(Whole));
    }
    const int64 Minutes = FMath::RoundToInt64(Time / SecondsPerMinute);
    if (Minutes < MinutesUpTo)
    {
        return FString::Printf(TEXT("%lld MIN"), static_cast<long long>(Minutes));
    }
    const int64 TenthsOfAnHour = FMath::RoundToInt64(Time / SecondsPerTenthOfAnHour);
    if (TenthsOfAnHour < TenthsOfAnHourUpTo)
    {
        return FString::Printf(TEXT("%lld.%lld H"), static_cast<long long>(TenthsOfAnHour / 10),
                               static_cast<long long>(TenthsOfAnHour % 10));
    }
    return FString::Printf(TEXT("%lld D"), static_cast<long long>(FMath::RoundToInt64(Time / SecondsPerDay)));
}

FString NavText::StarClass(EStarClass Class)
{
    switch (Class)
    {
    case EStarClass::M: return TEXT("red dwarf");
    case EStarClass::K: return TEXT("orange star");
    case EStarClass::G: return TEXT("yellow star");
    case EStarClass::F: return TEXT("yellow-white star");
    case EStarClass::A: return TEXT("white star");
    case EStarClass::B: return TEXT("blue-white star");
    }
    return TEXT("star");
}

FString NavText::WorldKind(EPlanetKind Kind)
{
    // One taxonomy: ds.Universe.Describe's words, so the console and the map
    // never call one world two things.
    return SystemDescription::KindName(Kind);
}

FString NavText::WorldName(const FPlanet& Planet)
{
    return Planet.GivenName.IsEmpty() ? Planet.Designation : Planet.GivenName + Separator + Planet.Designation;
}

FString NavText::Place(const FString& Name, EStarClass Class)
{
    return Name + Separator + StarClass(Class);
}

FString NavText::Place(const FString& Name, EStarClass Class, bool bVisited)
{
    return bVisited ? Place(Name, Class) + Separator + Visited(true) : Place(Name, Class);
}

FString NavText::Visited(bool bVisited)
{
    return bVisited ? FString(TEXT("visited")) : FString();
}

FString NavText::Distance(double Cm)
{
    return FString::Printf(TEXT("%.1f ly"), Cm / UniverseUnits::CmPerLightYear);
}

FString NavText::Course(const FString& CourseName, const TOptional<FVector>& ShipLocalDir, double ConeRadians)
{
    return ShipLocalDir ? CourseName + Separator + Bearing(*ShipLocalDir, ConeRadians) : CourseName;
}

FString NavText::NoCourse()
{
    return TEXT("None");
}

FString NavText::JumpWord(EJumpState State)
{
    switch (State)
    {
    case EJumpState::Idle:    return TEXT("Idle");
    case EJumpState::Winding: return TEXT("Winding");
    case EJumpState::Ready:   return TEXT("Ready");
    case EJumpState::Transit: return TEXT("Between stars");
    }
    return TEXT("Idle");
}

FString NavText::JumpWord(EJumpState State, bool bInSystem)
{
    return bInSystem && State == EJumpState::Transit ? FString(TEXT("In the fold")) : JumpWord(State);
}

FString NavText::Jump(EJumpState State)
{
    switch (State)
    {
    case EJumpState::Idle:    return TEXT("JUMP IDLE");
    case EJumpState::Winding: return TEXT("JUMP WINDING");
    case EJumpState::Ready:   return TEXT("JUMP READY");
    case EJumpState::Transit: return TEXT("BETWEEN STARS");
    }
    return TEXT("JUMP IDLE");
}

FString NavText::Jump(EJumpState State, bool bInSystem)
{
    return bInSystem && State == EJumpState::Transit ? FString(TEXT("IN THE FOLD")) : Jump(State);
}

FString NavText::Jump(EJumpState State, const FString& CourseName, const FVector& ShipLocalDir,
                      double ConeRadians)
{
    return Jump(State, false, CourseName, ShipLocalDir, ConeRadians);
}

FString NavText::Jump(EJumpState State, bool bInSystem, const FString& CourseName, const FVector& ShipLocalDir,
                      double ConeRadians)
{
    FString Line = Jump(State, bInSystem) + Separator + CourseName;
    if (State != EJumpState::Transit)
    {
        Line += Separator + Bearing(ShipLocalDir, ConeRadians);
    }
    return Line;
}
