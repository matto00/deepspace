#include "Ship/ShipDressingConfig.h"

#include "Core/GameIniReload.h"
#include "Ship/ShipDressing.h"

DEFINE_LOG_CATEGORY_STATIC(LogShipDressingConfig, Log, All);

namespace
{
    template <typename T, typename FNameOf>
    void Overlay(TArray<T>& Entries, T Entry, FNameOf NameOf)
    {
        const FName Name = NameOf(Entry);
        if (T* Existing = Entries.FindByPredicate([&](const T& E) { return NameOf(E) == Name; }))
        {
            *Existing = MoveTemp(Entry);
        }
        else
        {
            Entries.Add(MoveTemp(Entry));
        }
    }

    TArray<FDressWeight> Weights(const TArray<FDressConfigWeight>& Rows)
    {
        TArray<FDressWeight> Out;
        for (const FDressConfigWeight& Row : Rows)
        {
            Out.Add(FDressWeight{ Row.Name, Row.Weight });
        }
        return Out;
    }
}

UShipDressingConfig::UShipDressingConfig()
{
    ResetToCodeDefaults();
}

void UShipDressingConfig::ResetToCodeDefaults()
{
    const FShipDressingRules Defaults;
#define DS_FROM_RULES(Name) Name = Defaults.Name;
    DS_DRESS_SCALARS(DS_FROM_RULES)
#undef DS_FROM_RULES
    StackCap = Defaults.StackCap;
    TurnWeights = TArray<double>(Defaults.TurnWeights, UE_ARRAY_COUNT(Defaults.TurnWeights));
    Kinds.Reset();
    Templates.Reset();
    Colours.Reset();
}

FShipDressingRules UShipDressingConfig::ToRules(TArray<FString>* OutRefusals) const
{
    TArray<FString> Refused;
    FShipDressingRules Rules;
#define DS_TO_RULES(Name) Rules.Name = Name;
    DS_DRESS_SCALARS(DS_TO_RULES)
#undef DS_TO_RULES
    Rules.StackCap = StackCap;

    if (TurnWeights.Num() == UE_ARRAY_COUNT(Rules.TurnWeights))
    {
        for (int32 I = 0; I < TurnWeights.Num(); ++I)
        {
            Rules.TurnWeights[I] = TurnWeights[I];
        }
    }
    else
    {
        Refused.Add(FString::Printf(TEXT("TurnWeights: %d lines; it takes four, one per quarter turn"), TurnWeights.Num()));
    }

    for (const FDressConfigTemplate& Row : Templates)
    {
        FDressTemplate Template;
        Template.Name = Row.Name;
        Template.bStacks = Row.bStacks;
        Template.bAlternates = Row.bAlternates;
        for (int32 I = 0; I < Row.Parts.Num(); ++I)
        {
            const FDressConfigPart& Part = Row.Parts[I];
            EDressMesh Mesh = EDressMesh::Cube;
            if (Part.Mesh == FName(TEXT("Chamfer")))
            {
                Mesh = EDressMesh::Chamfer;
            }
            else if (Part.Mesh == FName(TEXT("Cylinder")))
            {
                Mesh = EDressMesh::Cylinder;
            }
            else if (Part.Mesh != FName(TEXT("Cube")))
            {
                Refused.Add(FString::Printf(TEXT("Templates %s part %d: Mesh=%s; it is Cube, Chamfer or Cylinder"),
                                            *Row.Name.ToString(), I, *Part.Mesh.ToString()));
            }
            Template.Parts.Add(FDressPart{ Mesh, Part.At, Part.Size, Part.Role });
        }
        Overlay(Rules.Templates, MoveTemp(Template), [](const FDressTemplate& T) { return T.Name; });
    }

    for (const FDressConfigKind& Row : Kinds)
    {
        FDressKind Kind{ Row.Kind, Row.Lambda, Weights(Row.Mix) };
        if (Kind.Mix.IsEmpty())
        {
            if (const FDressKind* Code = Rules.Kinds.FindByPredicate([&](const FDressKind& K) { return K.Kind == Row.Kind; }))
            {
                Kind.Mix = Code->Mix;
            }
        }
        Overlay(Rules.Kinds, MoveTemp(Kind), [](const FDressKind& K) { return K.Kind; });
    }

    for (const FDressConfigWeight& Row : Colours)
    {
        Overlay(Rules.Colours, FDressWeight{ Row.Name, Row.Weight }, [](const FDressWeight& W) { return W.Name; });
    }

    Refused.Append(DressRuleDomain::Refusals(Rules));
    if (OutRefusals)
    {
        *OutRefusals = MoveTemp(Refused);
    }
    return Rules;
}

void UShipDressingConfig::PostInitProperties()
{
    Super::PostInitProperties();
    // The class default has read the ini by now. A refusal at start-up must
    // leave a dressed ship in the code defaults, not a crash in Dress.
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        AcceptOrRefuse();
    }
}

void UShipDressingConfig::PostReloadConfig(FProperty* PropertyThatWasLoaded)
{
    Super::PostReloadConfig(PropertyThatWasLoaded);
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        AcceptOrRefuse();
    }
}

void UShipDressingConfig::AcceptOrRefuse()
{
    TArray<FString> Refused;
    FShipDressingRules Read = ToRules(&Refused);
    Refusals = MoveTemp(Refused);
    if (Refusals.IsEmpty())
    {
        Accepted = MoveTemp(Read);
        return;
    }
    // Refused whole: a half-applied tune is a ship nobody asked for. The
    // properties keep what the ini said, so the refusal can be read against
    // it; the rules in use are the last ones taken.
    for (const FString& Refusal : Refusals)
    {
        UE_LOG(LogShipDressingConfig, Warning, TEXT("DefaultGame.ini [%s] refused: %s"), *GetClass()->GetPathName(), *Refusal);
    }
}

TArray<FString> UShipDressingConfig::ReloadFromIni()
{
    GameIniReload::RereadFromDisk(StaticClass());
    return ApplyConfigCache();
}

TArray<FString> UShipDressingConfig::ApplyConfigCache()
{
    UShipDressingConfig* Defaults = GetMutableDefault<UShipDressingConfig>();
    // An array whose key is gone from the ini is left alone by the config
    // read, so without this a deleted row would outlive its line.
    Defaults->ResetToCodeDefaults();
    Defaults->ReloadConfig();
    return Defaults->GetRefusals();
}
