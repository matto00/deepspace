#include "Ship/ShipDressingRules.h"

namespace
{
    FDressPart Part(EDressMesh Mesh, const FVector& At, const FVector& Size, const TCHAR* Role)
    {
        return FDressPart{ Mesh, At, Size, FName(Role) };
    }

    FDressKind Kind(const TCHAR* Name, double Lambda, TArray<FDressWeight> Mix)
    {
        return FDressKind{ FName(Name), Lambda, MoveTemp(Mix) };
    }

    FDressTemplate Template(const TCHAR* Name, TArray<FDressPart> Parts, bool bStacks = false, bool bAlternates = false)
    {
        FDressTemplate Out;
        Out.Name = FName(Name);
        Out.Parts = MoveTemp(Parts);
        Out.bStacks = bStacks;
        Out.bAlternates = bAlternates;
        return Out;
    }

    const FName FabricRole(TEXT("fabric"));
}

FVector2D FDressTemplate::HalfExtent() const
{
    FVector2D Half = FVector2D::ZeroVector;
    for (const FDressPart& P : Parts)
    {
        Half.X = FMath::Max(Half.X, FMath::Abs(P.At.X) + P.Size.X * 0.5);
        Half.Y = FMath::Max(Half.Y, FMath::Abs(P.At.Y) + P.Size.Y * 0.5);
    }
    return Half;
}

double FDressTemplate::Height() const
{
    double Top = 0.0;
    for (const FDressPart& P : Parts)
    {
        Top = FMath::Max(Top, P.At.Z + P.Size.Z * 0.5);
    }
    return Top;
}

bool FDressTemplate::TakesColour() const
{
    return Parts.ContainsByPredicate([](const FDressPart& P) { return P.Role == FabricRole; });
}

FShipDressingRules::FShipDressingRules()
{
    using M = EDressMesh;

    // -- what can be left: primitives only, no modelling -----------------------
    //
    // Each is centred on its footprint and rests on z = 0 (the catalogue test
    // holds every one to that), long axis along Y.
    Templates = {
        Template(TEXT("mug"), {
            Part(M::Cylinder, FVector(-1.0, 0.0, 5.0), FVector(8.0, 8.0, 10.0), TEXT("ceramic")),
            Part(M::Cube, FVector(4.0, 0.0, 5.0), FVector(2.0, 1.5, 6.0), TEXT("ceramic")),
        }),
        Template(TEXT("plate"), {
            Part(M::Cylinder, FVector(0.0, 0.0, 1.0), FVector(22.0, 22.0, 2.0), TEXT("ceramic")),
        }),
        // A steel flask.
        Template(TEXT("bottle"), {
            Part(M::Cylinder, FVector(0.0, 0.0, 9.0), FVector(7.0, 7.0, 18.0), TEXT("metal")),
            Part(M::Cylinder, FVector(0.0, 0.0, 20.0), FVector(3.0, 3.0, 4.0), TEXT("metal")),
        }),
        // A food tin with its paper label.
        Template(TEXT("tin"), {
            Part(M::Cylinder, FVector(0.0, 0.0, 6.0), FVector(9.0, 9.0, 12.0), TEXT("metal")),
            Part(M::Cylinder, FVector(0.0, 0.0, 6.0), FVector(9.2, 9.2, 7.0), TEXT("paper")),
        }),
        // Lit: the ship is on, so is the tablet somebody left face up.
        Template(TEXT("tablet"), {
            Part(M::Cube, FVector(0.0, 0.0, 0.5), FVector(17.0, 24.0, 1.0), TEXT("rubber")),
            Part(M::Cube, FVector(0.0, 0.0, 1.05), FVector(15.0, 21.0, 0.1), TEXT("screen")),
        }),
        // Boards and a paper fore-edge showing on +X.
        Template(TEXT("book"), {
            Part(M::Cube, FVector(-0.5, 0.0, 1.5), FVector(14.0, 22.0, 3.0), TEXT("fabric")),
            Part(M::Cube, FVector(0.5, 0.0, 1.5), FVector(14.0, 21.0, 2.4), TEXT("paper")),
        }, /*bStacks*/ true, /*bAlternates*/ true),
        Template(TEXT("toolbox"), {
            Part(M::Cube, FVector(0.0, 0.0, 8.0), FVector(18.0, 40.0, 16.0), TEXT("paint_red")),
            Part(M::Cube, FVector(0.0, 0.0, 17.5), FVector(2.0, 20.0, 3.0), TEXT("metal")),
        }),
        Template(TEXT("cable_coil"), {
            Part(M::Cylinder, FVector(0.0, 0.0, 2.5), FVector(25.0, 25.0, 5.0), TEXT("rubber")),
        }),
        // A machined something, off whatever is being fixed.
        Template(TEXT("part"), {
            Part(M::Chamfer, FVector(0.0, 0.0, 4.0), FVector(10.0, 14.0, 8.0), TEXT("metal")),
            Part(M::Cylinder, FVector(0.0, 0.0, 9.0), FVector(6.0, 6.0, 2.0), TEXT("metal")),
        }),
        Template(TEXT("canister"), {
            Part(M::Cylinder, FVector(0.0, 0.0, 15.0), FVector(18.0, 18.0, 30.0), TEXT("paint_yellow")),
            Part(M::Cylinder, FVector(0.0, 0.0, 31.0), FVector(6.0, 6.0, 2.0), TEXT("metal")),
        }),
        // A canvas stowage crate with a steel strap round it.
        Template(TEXT("crate_small"), {
            Part(M::Chamfer, FVector(0.0, 0.0, 12.5), FVector(30.0, 40.0, 25.0), TEXT("fabric")),
            Part(M::Cube, FVector(0.0, 0.0, 12.5), FVector(31.0, 41.0, 3.0), TEXT("metal")),
        }, /*bStacks*/ true),
        Template(TEXT("boots"), {
            Part(M::Cube, FVector(0.0, -5.5, 4.0), FVector(25.0, 9.0, 8.0), TEXT("rubber")),
            Part(M::Cube, FVector(-7.5, -5.5, 14.0), FVector(10.0, 9.0, 20.0), TEXT("rubber")),
            Part(M::Cube, FVector(0.0, 5.5, 4.0), FVector(25.0, 9.0, 8.0), TEXT("rubber")),
            Part(M::Cube, FVector(-7.5, 5.5, 14.0), FVector(10.0, 9.0, 20.0), TEXT("rubber")),
        }),
        // Folded.
        Template(TEXT("blanket"), {
            Part(M::Cube, FVector(0.0, 0.0, 4.0), FVector(30.0, 40.0, 8.0), TEXT("fabric")),
        }),
    };

    // -- where, and what: each kind of surface's affinity and its mix ----------
    //
    // A weighted mix per surface, never the whole catalogue uniformly: that
    // is what put the reactor in the galley.
    Kinds = {
        // Where the cooking happens: the busiest surface aboard.
        Kind(TEXT("counter.top"), 4.0,
             { { TEXT("mug"), 4.0 }, { TEXT("plate"), 3.0 }, { TEXT("tin"), 3.0 }, { TEXT("bottle"), 2.0 }, { TEXT("tablet"), 1.0 } }),
        Kind(TEXT("galley_table.top"), 2.5,
             { { TEXT("mug"), 4.0 }, { TEXT("plate"), 3.0 }, { TEXT("tablet"), 2.0 }, { TEXT("book"), 1.0 }, { TEXT("bottle"), 1.0 } }),
        Kind(TEXT("desk.top"), 3.0,
             { { TEXT("book"), 4.0 }, { TEXT("mug"), 2.0 }, { TEXT("tablet"), 2.0 }, { TEXT("tin"), 1.0 }, { TEXT("part"), 1.0 } }),
        // Whatever is being fixed is always half taken apart.
        Kind(TEXT("workbench.top"), 5.0,
             { { TEXT("part"), 4.0 }, { TEXT("cable_coil"), 3.0 }, { TEXT("toolbox"), 3.0 }, { TEXT("canister"), 1.0 } }),
        // Every shelf of the rack is the same kind of place: stowage.
        Kind(TEXT("wall_rack"), 2.0,
             { { TEXT("crate_small"), 3.0 }, { TEXT("canister"), 3.0 }, { TEXT("toolbox"), 2.0 }, { TEXT("cable_coil"), 2.0 },
               { TEXT("part"), 2.0 }, { TEXT("book"), 1.0 } }),
        // The cockpit is kept clear; a mug left on a wing is what says so.
        Kind(TEXT("cockpit_desk"), 0.8,
             { { TEXT("mug"), 3.0 }, { TEXT("tablet"), 2.0 }, { TEXT("book"), 1.0 } }),
        Kind(TEXT("locker.top"), 0.5,
             { { TEXT("crate_small"), 2.0 }, { TEXT("blanket"), 2.0 }, { TEXT("book"), 1.0 } }),
        Kind(TEXT("airlock_bench.seat"), 0.7,
             { { TEXT("boots"), 3.0 }, { TEXT("blanket"), 1.0 }, { TEXT("tablet"), 1.0 } }),
    };

    Colours = {
        { TEXT("olive"), 5.0 },
        { TEXT("navy"), 3.0 },
        { TEXT("rust"), 2.0 },
        { TEXT("ochre"), 1.0 },
    };
}

const FDressKind* FShipDressingRules::FindKind(FName InKind) const
{
    if (const FDressKind* Exact = Kinds.FindByPredicate([InKind](const FDressKind& K) { return K.Kind == InKind; }))
    {
        return Exact;
    }
    FString Prop;
    if (!InKind.ToString().Split(TEXT("."), &Prop, nullptr))
    {
        return nullptr;
    }
    const FName ByProp(*Prop);
    return Kinds.FindByPredicate([ByProp](const FDressKind& K) { return K.Kind == ByProp; });
}

const FDressTemplate* FShipDressingRules::FindTemplate(FName Name) const
{
    return Templates.FindByPredicate([Name](const FDressTemplate& T) { return T.Name == Name; });
}
