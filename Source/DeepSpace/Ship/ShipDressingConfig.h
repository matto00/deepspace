#pragma once

#include "CoreMinimal.h"
#include "Ship/ShipDressingRules.h"
#include "UObject/Object.h"
#include "ShipDressingConfig.generated.h"

/*
 * The ini rows for the dressing's tables. Declared here, never in
 * ShipDressingRules.h, so the pure struct carries no reflection (lived-in
 * decision 1c). Each is FShipDressingRules's own type, spelt for the ini.
 */

/** A name and its weight: an entry of a mix, or a colour. */
USTRUCT()
struct FDressConfigWeight
{
    GENERATED_BODY()

    UPROPERTY()
    FName Name;

    UPROPERTY()
    double Weight = 0.0;
};

/** One kind of surface: +Kinds=(Kind="desk.top",Lambda=3,Mix=((Name="book",Weight=4),...)) */
USTRUCT()
struct FDressConfigKind
{
    GENERATED_BODY()

    UPROPERTY()
    FName Kind;

    UPROPERTY()
    double Lambda = 0.0;

    /** Empty keeps the code's mix for this kind, so a row can move only its
     *  mean: a mix of nothing would dress nothing, which Lambda=0 says. */
    UPROPERTY()
    TArray<FDressConfigWeight> Mix;
};

/** One primitive: Mesh is Cube, Chamfer or Cylinder. */
USTRUCT()
struct FDressConfigPart
{
    GENERATED_BODY()

    UPROPERTY()
    FName Mesh;

    UPROPERTY()
    FVector At = FVector::ZeroVector;

    UPROPERTY()
    FVector Size = FVector::ZeroVector;

    UPROPERTY()
    FName Role;
};

/** One thing that can be left: +Templates=(Name="mug",Parts=((Mesh=Cylinder,At=(X=-1,Y=0,Z=5),Size=(X=8,Y=8,Z=10),Role="ceramic"),...)) */
USTRUCT()
struct FDressConfigTemplate
{
    GENERATED_BODY()

    UPROPERTY()
    FName Name;

    UPROPERTY()
    TArray<FDressConfigPart> Parts;

    UPROPERTY()
    bool bStacks = false;

    UPROPERTY()
    bool bAlternates = false;
};

/**
 * The ini over FShipDressingRules: section [/Script/DeepSpace.ShipDressingConfig]
 * of Config/DefaultGame.ini (lived-in decision 1c, the shape of procgen's
 * UProcGenPriorsConfig). A tune is an ini edit and ds.Dress.Reload in a
 * running session: no rebuild, no restart, and the ship stays where it is.
 *
 * The scalars mirror the rules field by field, each starting at
 * FShipDressingRules{}'s value. The tables are overlays: a Kinds, Templates
 * or Colours row replaces the code's entry of the same name, or adds one,
 * and everything the ini does not name stays as the code has it. So the
 * section lists only what is being tuned -- a mug's height is one row, not
 * a restatement of the whole catalogue.
 *
 * Guarantees are not here (DressGuarantees): nothing in the ini can put a
 * mug through a shelf. What it can say is held to DressRuleDomain, and a
 * read outside it is refused whole, the rules in use staying as they were.
 * The class default is the one live copy; UShipDressingSubsystem asks it at
 * every dress and keeps nothing.
 *
 * LivedIn is not here: it is ds.Dress.LivedIn, the scalar a playtest pokes.
 * The field comments are in ShipDressingRules.h, where the numbers mean
 * something.
 */
UCLASS(Config = Game)
class DEEPSPACE_API UShipDressingConfig : public UObject
{
    GENERATED_BODY()

public:
    UShipDressingConfig();

    /** The code defaults with these properties laid over them, as they stand,
     *  and whatever of that is outside the domain. Not what dresses the ship:
     *  that is GetRules(), which only ever holds a read that was taken. */
    FShipDressingRules ToRules(TArray<FString>* OutRefusals = nullptr) const;

    /** On the class default: the last rules taken, code defaults until the
     *  ini is. What every dress is drawn with. */
    const FShipDressingRules& GetRules() const { return Accepted; }

    /** Why the last read was refused, one line each; empty if it was taken. */
    const TArray<FString>& GetRefusals() const { return Refusals; }

    /** Re-reads DefaultGame.ini from disk, then ApplyConfigCache. What
     *  ds.Dress.Reload does. Returns the refusals, empty if taken. */
    static TArray<FString> ReloadFromIni();

    /** Reads this class's section of the config cache into the class
     *  default: every property back to its code default first, so a line
     *  deleted from the ini falls back as it would at start-up, then the
     *  lines present, then taken or refused whole. */
    static TArray<FString> ApplyConfigCache();

    virtual void PostInitProperties() override;
    virtual void PostReloadConfig(FProperty* PropertyThatWasLoaded) override;

    UPROPERTY(Config) double AlongUseA;
    UPROPERTY(Config) double AlongUseB;
    UPROPERTY(Config) double AlongCentreA;
    UPROPERTY(Config) double AlongCentreB;
    UPROPERTY(Config) double BackA;
    UPROPERTY(Config) double BackB;
    UPROPERTY(Config) double StackChance;
    UPROPERTY(Config) double WearA;
    UPROPERTY(Config) double WearB;
    UPROPERTY(Config) double ReplacedBelow;
    UPROPERTY(Config) double FadedAbove;

    UPROPERTY(Config) int32 StackCap;

    /** Four lines, one per quarter turn, or the read is refused. */
    UPROPERTY(Config) TArray<double> TurnWeights;

    UPROPERTY(Config) TArray<FDressConfigKind> Kinds;
    UPROPERTY(Config) TArray<FDressConfigTemplate> Templates;
    UPROPERTY(Config) TArray<FDressConfigWeight> Colours;

    /** Back to code defaults, overlays empty: what an ini with no section
     *  lines leaves. */
    void ResetToCodeDefaults();

private:
    void AcceptOrRefuse();

    /** Not a UPROPERTY, so nothing but AcceptOrRefuse writes it. */
    FShipDressingRules Accepted;

    TArray<FString> Refusals;
};
