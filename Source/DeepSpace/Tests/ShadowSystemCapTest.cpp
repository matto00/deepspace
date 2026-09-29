#include "Misc/AutomationTest.h"
#include "Sky/ShipSky.h"
#include "Surface/SunShadowMap.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowSystemCapTest, "DeepSpace.Sky.ShadowSystemCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShadowUploadPiecesTest, "DeepSpace.Sky.ShadowUploadPieces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace ShadowSystemCapLocal
{
    constexpr double EarthRadiusCm = 6.371e8;

    /** Baemsekai IV as the cap sees it: 0.74 Earth radii and a lowest row
     *  5.21 degrees under the level (the cast-shadow plan's planning). */
    ShipSky::FShadowWorld IVLike(double RadiusEarth = 0.74)
    {
        return { RadiusEarth * EarthRadiusCm, FMath::DegreesToRadians(-5.21) };
    }

    int64 Total(TConstArrayView<ShipSky::FShadowWorld> Worlds, const TArray<int32>& Widths)
    {
        int64 Bytes = 0;
        for (int32 Index = 0; Index < Worlds.Num(); ++Index)
        {
            Bytes += ShipSky::ShadowLevelsBytes(Widths[Index], SunShadowMap::RowsFor(Widths[Index], Worlds[Index].PsiLo));
        }
        return Bytes;
    }

    /** A map of Width x Rows, its levels built as the bake builds them. */
    FSunShadowMap Built(int32 Width, int32 Rows)
    {
        FSunShadowMap Map;
        Map.Width = Width;
        Map.Rows = Rows;
        Map.Levels.SetNum(1);
        Map.Levels[0].Init(0, Width * Rows);
        SunShadowMap::BuildLevels(Map);
        return Map;
    }
}

/**
 * The per-system GPU cap on the orbit's shadow maps (the developer's ruling,
 * 2026-09-28: 128 MB, a system with many worlds lowering its maps'
 * resolution to fit). The bytes are the levels' own, as BuildLevels makes
 * them; a system under the cap keeps the full width everywhere; one over it
 * halves the map whose texel is finest first, the first of equals first,
 * until it fits, and never under 256 columns. Pure.
 */
bool FShadowSystemCapTest::RunTest(const FString& Parameters)
{
    using namespace ShadowSystemCapLocal;
    for (const FIntPoint Size : { FIntPoint(4096, 1084), FIntPoint(256, 69), FIntPoint(2048, 541), FIntPoint(512, 1), FIntPoint(1, 1) })
    {
        TestEqual(FString::Printf(TEXT("%d x %d: the levels' bytes are what BuildLevels makes"), Size.X, Size.Y),
            ShipSky::ShadowLevelsBytes(Size.X, Size.Y), Built(Size.X, Size.Y).Bytes());
    }
    TestEqual(TEXT("no map, no bytes"), ShipSky::ShadowLevelsBytes(0, 0), static_cast<int64>(0));

    // The start system: five worlds, about 64 MB at 4096 -- under the cap, untouched.
    {
        TArray<ShipSky::FShadowWorld> Worlds;
        for (const double Radius : { 0.68, 0.74, 0.55, 0.74, 0.62 })
        {
            Worlds.Add(IVLike(Radius));
        }
        const TArray<int32> Widths = ShipSky::CappedShadowWidths(Worlds, 4096);
        TestTrue(TEXT("five worlds at 4096 are under 128 MB"), Total(Worlds, Widths) <= ShipSky::ShadowSystemCapBytes);
        TestTrue(TEXT("and keep 4096 everywhere"), !Widths.ContainsByPredicate([](int32 W) { return W != 4096; }));
    }

    // Sixteen IV-like worlds (the corpus's worst system was put at about 16):
    // about 205 MB at 4096. Halved, first of equals first, just until they fit.
    {
        TArray<ShipSky::FShadowWorld> Worlds;
        Worlds.Init(IVLike(), 16);
        TestTrue(TEXT("sixteen worlds at 4096 are over the cap"), Total(Worlds, TArray<int32>([&] { TArray<int32> W; W.Init(4096, 16); return W; }())) > ShipSky::ShadowSystemCapBytes);
        const TArray<int32> Widths = ShipSky::CappedShadowWidths(Worlds, 4096);
        const int64 Held = Total(Worlds, Widths);
        AddInfo(FString::Printf(TEXT("sixteen IV-like worlds: %.1f MB after the cap"), Held / 1048576.0));
        TestTrue(TEXT("and fit it after"), Held <= ShipSky::ShadowSystemCapBytes);
        int32 Halved = 0;
        for (int32 Index = 0; Index < Widths.Num(); ++Index)
        {
            TestTrue(TEXT("each at 4096 or 2048"), Widths[Index] == 4096 || Widths[Index] == 2048);
            Halved += Widths[Index] == 2048 ? 1 : 0;
            TestTrue(TEXT("the first of equals halved first"), Index == 0 || Widths[Index] >= Widths[Index - 1]);
        }
        const int64 OneBack = ShipSky::ShadowLevelsBytes(4096, SunShadowMap::RowsFor(4096, Worlds[0].PsiLo))
                            - ShipSky::ShadowLevelsBytes(2048, SunShadowMap::RowsFor(2048, Worlds[0].PsiLo));
        TestTrue(FString::Printf(TEXT("no more halved than it takes (%d halved)"), Halved), Halved > 0 && Held + OneBack > ShipSky::ShadowSystemCapBytes);
    }

    // The finest texel goes first: a small world before a large one.
    {
        const TArray<ShipSky::FShadowWorld> Worlds = { IVLike(2.13), IVLike(0.44) };
        const int64 Both = Total(Worlds, { 4096, 4096 });
        const TArray<int32> Widths = ShipSky::CappedShadowWidths(Worlds, 4096, Both - 1);
        TestEqual(TEXT("the large world keeps its width"), Widths[0], 4096);
        TestEqual(TEXT("the small one, whose texel is finer, is halved"), Widths[1], 2048);
        const TArray<int32> Harder = ShipSky::CappedShadowWidths(Worlds, 4096, Total(Worlds, { 4096, 1024 }));
        TestEqual(TEXT("halved again while its texel is still the finer"), Harder[1], 1024);
        TestEqual(TEXT("and the large one then"), Harder[0], 4096);
    }

    // A cap nothing meets stops at the floor rather than going on.
    {
        TArray<ShipSky::FShadowWorld> Worlds;
        Worlds.Init(IVLike(), 64);
        const TArray<int32> Widths = ShipSky::CappedShadowWidths(Worlds, 4096, 1);
        TestTrue(TEXT("an impossible cap leaves every map at 256 columns"), !Widths.ContainsByPredicate([](int32 W) { return W != ShipSky::ShadowMinWidth; }));
    }
    TestEqual(TEXT("no worlds, no widths"), ShipSky::CappedShadowWidths({}, 4096).Num(), 0);
    return true;
}

/**
 * A landed map goes to the GPU in pieces of whole rows (the developer's
 * ruling, 2026-09-28: uploaded in pieces on the render thread over several
 * frames). Every row of every level is in exactly one piece, level 0 first
 * and each level's rows in order, and a piece holds at most the bytes asked
 * -- or one row, where a row is wider. Pure.
 */
bool FShadowUploadPiecesTest::RunTest(const FString& Parameters)
{
    using namespace ShadowSystemCapLocal;
    for (const int64 PieceBytes : { static_cast<int64>(16 * 1024), static_cast<int64>(1024 * 1024), static_cast<int64>(100), TNumericLimits<int64>::Max() })
    {
        const FSunShadowMap Map = Built(512, 135);
        const TArray<ShipSky::FShadowPiece> Pieces = ShipSky::ShadowUploadPieces(Map, PieceBytes);
        int32 Level = 0;
        int32 Row = 0;
        bool bInOrder = true;
        bool bWithin = true;
        int64 Bytes = 0;
        for (const ShipSky::FShadowPiece& Piece : Pieces)
        {
            if (Piece.Level != Level)
            {
                bInOrder &= Row == Map.RowsAt(Level) && Piece.Level == Level + 1;
                Level = Piece.Level;
                Row = 0;
            }
            bInOrder &= Piece.FirstRow == Row && Piece.Rows > 0;
            Row += Piece.Rows;
            const int64 RowBytes = static_cast<int64>(Map.WidthAt(Piece.Level)) * 2;
            bWithin &= Piece.Bytes == Piece.Rows * RowBytes && (Piece.Bytes <= PieceBytes || Piece.Rows == 1);
            Bytes += Piece.Bytes;
        }
        const FString At = FString::Printf(TEXT("pieces of %lld bytes"), PieceBytes);
        TestTrue(At + TEXT(": every row of every level once, level 0 first, in order"), bInOrder && Level == Map.LevelCount() - 1 && Row == Map.RowsAt(Level));
        TestTrue(At + TEXT(": each within the bytes asked, or one row"), bWithin);
        TestEqual(At + TEXT(": together the whole map"), Bytes, Map.Bytes());
    }
    TestEqual(TEXT("a map at 16 KB a piece takes many pieces"), ShipSky::ShadowUploadPieces(Built(512, 135), 16 * 1024).Num() > 8, true);
    TestEqual(TEXT("and whole, one a level"), ShipSky::ShadowUploadPieces(Built(512, 135), TNumericLimits<int64>::Max()).Num(), Built(512, 135).LevelCount());
    TestEqual(TEXT("a map with no levels has no pieces"), ShipSky::ShadowUploadPieces(FSunShadowMap(), 1024).Num(), 0);
    return true;
}

#endif
