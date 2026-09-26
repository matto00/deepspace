#include "Ship/ShipDressing.h"

#include "Universe/GenSeed.h"
#include "Universe/GenStream.h"

namespace
{
    /** cm. Things may touch -- a mug against a plate, a pile against the
     *  wall -- and only a real overlap is refused. */
    constexpr double Touch = 1e-3;

    bool Overlaps(const FBox2D& A, const FBox2D& B)
    {
        return A.Min.X < B.Max.X - Touch && B.Min.X < A.Max.X - Touch
            && A.Min.Y < B.Max.Y - Touch && B.Min.Y < A.Max.Y - Touch;
    }

    bool Overlaps(const FBox& A, const FBox& B)
    {
        return A.Min.X < B.Max.X - Touch && B.Min.X < A.Max.X - Touch
            && A.Min.Y < B.Max.Y - Touch && B.Min.Y < A.Max.Y - Touch
            && A.Min.Z < B.Max.Z - Touch && B.Min.Z < A.Max.Z - Touch;
    }

    bool IsAscii(const FString& Text)
    {
        for (const TCHAR Char : Text)
        {
            if (static_cast<uint32>(Char) >= 128u)
            {
                return false;
            }
        }
        return true;
    }

    /** Names are hashed lower-cased: FName compares without case, and which
     *  spelling of a name the engine hands back depends on which was made
     *  first, so a seed must not depend on it. */
    FString Lower(FName Name)
    {
        return Name.ToString().ToLower();
    }

    bool Fits(const FDressTemplate& Template, const FVector2D& Size)
    {
        const FVector2D Half = Template.HalfExtent();
        return (2.0 * Half.X <= Size.X && 2.0 * Half.Y <= Size.Y)
            || (2.0 * Half.Y <= Size.X && 2.0 * Half.X <= Size.Y);
    }

    FTransform Lying(int32 Turns, const FVector& Where)
    {
        return FTransform(FRotator(0.0, 90.0 * (Turns & 3), 0.0).Quaternion(), Where);
    }
}

uint64 ShipDressing::DressSeed(uint64 Root)
{
    return GenSeed::Derive(GenSeed::Derive(Root, GenSeed::Label("ship")), GenSeed::Label("dressing"));
}

uint64 ShipDressing::SurfaceSeed(uint64 InDressSeed, const FDressSurface& Surface)
{
    const uint64 RoomSeed = GenSeed::Derive(InDressSeed, GenSeed::LabelText(TEXT("dress.") + Lower(Surface.Room)));
    return GenSeed::Derive(RoomSeed, GenSeed::LabelText(Lower(Surface.Kind)), static_cast<uint64>(Surface.Ordinal));
}

double ShipDressing::DrawAlong(FGenStream& Stream, EDressUse Use, const FShipDressingRules& Rules)
{
    // People leave things near where they sit and push them away from where
    // they work: the distance from the use end is Beta(2, 4), bunched near
    // it. A surface used from the middle clusters there, Beta(3, 3), and
    // thins towards both ends. Never uniform: a uniform scatter is what
    // reads as sprinkled rather than left.
    switch (Use)
    {
    case EDressUse::PosY:
        return 1.0 - Stream.Beta(Rules.AlongUseA, Rules.AlongUseB);
    case EDressUse::NegY:
        return Stream.Beta(Rules.AlongUseA, Rules.AlongUseB);
    case EDressUse::Centre:
    default:
        return Stream.Beta(Rules.AlongCentreA, Rules.AlongCentreB);
    }
}

double ShipDressing::DrawBack(FGenStream& Stream, const FShipDressingRules& Rules)
{
    // Things end up pushed back against the wall, not balanced on the front
    // lip: Beta(4, 2), leaning towards the back edge.
    return Stream.Beta(Rules.BackA, Rules.BackB);
}

FVector2D ShipDressing::TurnedHalfExtent(const FDressTemplate& Template, int32 Turns)
{
    const FVector2D Half = Template.HalfExtent();
    return (Turns & 1) ? FVector2D(Half.Y, Half.X) : Half;
}

FName ShipDressing::PartRole(const FDressPart& Part, FName Colour)
{
    static const FName Fabric(TEXT("fabric"));
    if (Part.Role == Fabric && !Colour.IsNone())
    {
        return FName(*FString::Printf(TEXT("fabric_%s"), *Colour.ToString()));
    }
    return Part.Role;
}

FBox ShipDressing::ItemBounds(const FDressItem& Item, const FDressSurface& Surface, const FShipDressingRules& Rules)
{
    FBox Bounds(ForceInit);
    if (const FDressTemplate* Template = Rules.FindTemplate(Item.Template))
    {
        for (const FDressPart& Part : Template->Parts)
        {
            Bounds += FBox(Part.At - Part.Size * 0.5, Part.At + Part.Size * 0.5).TransformBy(Item.World);
        }
    }
    return Bounds;
}

TArray<FDressItem> ShipDressing::Dress(TConstArrayView<FDressSurface> Surfaces, TConstArrayView<FBox> KeepOut,
                                       uint64 Seed, const FShipDressingRules& Rules)
{
    TArray<FDressItem> Out;

    const double LivedIn = FMath::Clamp(Rules.LivedIn, 0.0, DressGuarantees::MaxLivedIn);
    TArray<double> ColourWeights;
    for (const FDressWeight& Colour : Rules.Colours)
    {
        ColourWeights.Add(Colour.Weight);
    }
    const TArray<double> TurnWeights(Rules.TurnWeights, UE_ARRAY_COUNT(Rules.TurnWeights));

    for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
    {
        const FDressSurface& Surface = Surfaces[Index];
        if (Surface.ToWorld.GetLocation().Z < DressGuarantees::MinRestHeightCm
            || !IsAscii(Surface.Room.ToString()) || !IsAscii(Surface.Kind.ToString()))
        {
            continue;
        }
        const FDressKind* Kind = Rules.FindKind(Surface.Kind);
        if (!Kind)
        {
            continue;
        }

        // What could be left here: the kind's mix, less anything taller than
        // the surface's clearance or wider than the surface itself. What
        // cannot fit is never drawn, rather than drawn and thrown away.
        TArray<const FDressTemplate*> Candidates;
        TArray<double> Weights;
        double Total = 0.0;
        for (const FDressWeight& Entry : Kind->Mix)
        {
            const FDressTemplate* Template = Rules.FindTemplate(Entry.Name);
            const bool bFits = Template && Entry.Weight > 0.0 && Template->Height() <= Surface.Clear
                               && Fits(*Template, Surface.Size);
            Candidates.Add(Template);
            Weights.Add(bFits ? Entry.Weight : 0.0);
            Total += Weights.Last();
        }
        if (Total <= 0.0)
        {
            continue;
        }

        const uint64 Own = SurfaceSeed(Seed, Surface);

        // How many: things get left on a surface one at a time and
        // independently, which is what Poisson counts. Stops at the cap
        // rather than redrawing (plan conflict 11).
        FGenStream CountStream(GenSeed::Derive(Own, GenSeed::Label("count")));
        const double Mean = FMath::Min(Kind->Lambda * LivedIn, FGenStream::MaxPoissonMean);
        const int32 Count = CountStream.Poisson(Mean, DressGuarantees::PoissonMax);

        TArray<FBox2D> Taken;
        for (int32 ItemIndex = 0; ItemIndex < Count; ++ItemIndex)
        {
            // One stream per thing: what it is, its colour, its pile and
            // where it lands are one quantity (procgen decision 2).
            FGenStream Stream(GenSeed::Derive(Own, GenSeed::Label("item"), static_cast<uint64>(ItemIndex)));

            const FDressTemplate& Template = *Candidates[Stream.Categorical(Weights)];
            const FName Colour = Template.TakesColour() ? Rules.Colours[Stream.Categorical(ColourWeights)].Name : NAME_None;
            const double Height = Template.Height();

            // A pile: geometric, each one more on it an independent Chance,
            // up to the cap -- and never above what the surface can clear.
            int32 Pile = 1;
            if (Template.bStacks)
            {
                while (Pile < FMath::Max(1, Rules.StackCap) && Stream.Chance(Rules.StackChance))
                {
                    ++Pile;
                }
            }
            while (Pile > 1 && Pile * Height > Surface.Clear)
            {
                --Pile;
            }

            for (int32 Attempt = 0; Attempt < DressGuarantees::MaxAttempts; ++Attempt)
            {
                const int32 Turns = Stream.Categorical(TurnWeights);
                FVector2D Half = TurnedHalfExtent(Template, Turns);
                if (Template.bAlternates && Pile > 1)
                {
                    const double Widest = FMath::Max(Half.X, Half.Y);
                    Half = FVector2D(Widest, Widest);
                }
                const double Along = DrawAlong(Stream, Surface.Use, Rules);
                const double Back = DrawBack(Stream, Rules);

                // Drawn inside [half, L - half]: nothing can overhang its
                // surface to begin with, so containment is never a rejection.
                const FVector2D Free(Surface.Size.X - 2.0 * Half.X, Surface.Size.Y - 2.0 * Half.Y);
                if (Free.X < 0.0 || Free.Y < 0.0)
                {
                    continue;
                }
                const double Y = -0.5 * Surface.Size.Y + Half.Y + Along * Free.Y;
                const double FrontX = 0.5 * Surface.Size.X - Half.X;
                const double X = Surface.Back == EDressEdge::NegX ? FrontX - Back * Free.X : -FrontX + Back * Free.X;

                const FBox2D Footprint(FVector2D(X - Half.X, Y - Half.Y), FVector2D(X + Half.X, Y + Half.Y));
                if (Taken.ContainsByPredicate([&](const FBox2D& Other) { return Overlaps(Footprint, Other); })
                    || Surface.Excludes.ContainsByPredicate([&](const FBox2D& Other) { return Overlaps(Footprint, Other); }))
                {
                    continue;
                }
                const FBox Column = FBox(FVector(Footprint.Min, 0.0), FVector(Footprint.Max, Pile * Height))
                                        .TransformBy(Surface.ToWorld);
                if (KeepOut.ContainsByPredicate([&](const FBox& Zone) { return Overlaps(Column, Zone); }))
                {
                    continue;
                }

                Taken.Add(Footprint);
                for (int32 Level = 0; Level < Pile; ++Level)
                {
                    FDressItem& Item = Out.AddDefaulted_GetRef();
                    Item.Surface = Index;
                    Item.Template = Template.Name;
                    Item.Colour = Colour;
                    Item.At = FVector2D(X, Y);
                    Item.Base = Level * Height;
                    Item.Turns = (Turns + (Template.bAlternates ? Level : 0)) & 3;
                    Item.StackIndex = Level;
                    Item.World = Lying(Item.Turns, FVector(X, Y, Item.Base)) * Surface.ToWorld;
                }
                break;
            }
        }
    }
    return Out;
}

EDressWear ShipDressing::Wear(uint64 InDressSeed, FStringView Piece, const FShipDressingRules& Rules)
{
    const FString Name = FString(Piece).ToLower();
    if (!IsAscii(Name))
    {
        return EDressWear::Standard;
    }
    FGenStream Stream(GenSeed::Derive(GenSeed::Derive(InDressSeed, GenSeed::Label("wear")), GenSeed::LabelText(Name)));

    // A bounded proportion, most things well used: Beta(5, 2). Quantised into
    // three buckets because wear is a material swap, not a gradient.
    const double Worn = Stream.Beta(Rules.WearA, Rules.WearB);
    if (Worn < Rules.ReplacedBelow)
    {
        return EDressWear::Replaced;
    }
    if (Worn > Rules.FadedAbove)
    {
        return EDressWear::Faded;
    }
    return EDressWear::Standard;
}
