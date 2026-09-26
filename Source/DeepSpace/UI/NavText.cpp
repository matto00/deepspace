#include "UI/NavText.h"

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

FString NavText::Jump(EJumpState State, const FString& CourseName, const FVector& ShipLocalDir,
                      double ConeRadians)
{
    FString Line = Jump(State) + Separator + CourseName;
    if (State != EJumpState::Transit)
    {
        Line += Separator + Bearing(ShipLocalDir, ConeRadians);
    }
    return Line;
}
