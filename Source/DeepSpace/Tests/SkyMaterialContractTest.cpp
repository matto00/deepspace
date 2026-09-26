#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionParameter.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialParameterCollection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/IConsoleManager.h"
#include "Sky/ShipSky.h"
#include "Sky/SkyMaterialContract.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FSkyMaterialContractTest,
    "DeepSpace.Sky.MaterialContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
    struct FRole
    {
        const TCHAR* Key;
        FName Name;
        const TCHAR* Type;
    };

    /** The JSON's role keys against the header's names: the one place the
     *  two are written side by side. */
    TArray<FRole> Roles()
    {
        return {
            { TEXT("colour"), SkyMaterial::Colour, TEXT("vector") },
            { TEXT("light_direction"), SkyMaterial::LightDirection, TEXT("vector") },
            { TEXT("rim"), SkyMaterial::Rim, TEXT("vector") },
            { TEXT("brightness"), SkyMaterial::Brightness, TEXT("scalar") },
            { TEXT("point_blend"), SkyMaterial::PointBlend, TEXT("scalar") },
            { TEXT("mottle"), SkyMaterial::Mottle, TEXT("scalar") },
            { TEXT("interior_light"), SkyMaterial::InteriorLight, TEXT("scalar") },
            { TEXT("veil"), SkyMaterial::Veil, TEXT("scalar") },
        };
    }

    FString Describe(const TSet<FName>& Names)
    {
        TArray<FString> Sorted;
        for (const FName& Name : Names)
        {
            Sorted.Add(Name.ToString());
        }
        Sorted.Sort();
        return TEXT("{") + FString::Join(Sorted, TEXT(", ")) + TEXT("}");
    }

    bool SameSet(const TSet<FName>& A, const TSet<FName>& B)
    {
        return A.Num() == B.Num() && A.Includes(B);
    }

    /** One asset's parameters from the JSON, split by type. */
    void JsonNames(const TSharedPtr<FJsonObject>& Contract, const TSharedPtr<FJsonObject>& Entry,
                   TSet<FName>& OutScalars, TSet<FName>& OutVectors)
    {
        const TSharedPtr<FJsonObject> Parameters = Contract->GetObjectField(TEXT("parameters"));
        for (const TSharedPtr<FJsonValue>& Role : Entry->GetArrayField(TEXT("parameters")))
        {
            const TSharedPtr<FJsonObject> Parameter = Parameters->GetObjectField(Role->AsString());
            const FName Name(*Parameter->GetStringField(TEXT("name")));
            (Parameter->GetStringField(TEXT("type")) == TEXT("scalar") ? OutScalars : OutVectors).Add(Name);
        }
    }

    /**
     * Run the material translator on the graph, synchronously, and return its
     * errors. A graph that does not translate still saves and still lists its
     * parameters, and a commandlet compiles no shaders to complain with, so
     * this is the only place a broken graph shows before somebody looks.
     */
    TArray<FString> TranslationErrors(UMaterial* Material)
    {
        FMaterialResource Resource;
        Resource.SetMaterial(Material, nullptr, GMaxRHIShaderPlatform);
        FString Source;
        const bool bTranslated = Resource.GetMaterialExpressionSource(Source);
        TArray<FString> Errors = Resource.GetCompileErrors();
        if (!bTranslated && Errors.IsEmpty())
        {
            Errors.Add(TEXT("the translator failed without saying why"));
        }
        return Errors;
    }

    TSet<FName> AssetNames(const UMaterialInterface* Material, bool bScalars)
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        if (bScalars)
        {
            Material->GetAllScalarParameterInfo(Infos, Ids);
        }
        else
        {
            Material->GetAllVectorParameterInfo(Infos, Ids);
        }
        TSet<FName> Names;
        for (const FMaterialParameterInfo& Info : Infos)
        {
            Names.Add(Info.Name);
        }
        return Names;
    }
}

bool FSkyMaterialContractTest::RunTest(const FString& Parameters)
{
    const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/sky_material_contract.json"));
    FString Text;
    if (!TestTrue(TEXT("contract file loads: ") + Path, FFileHelper::LoadFileToString(Text, *Path)))
    {
        return false;
    }
    TSharedPtr<FJsonObject> Contract;
    if (!TestTrue(TEXT("contract parses as JSON"),
                  FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Contract) && Contract.IsValid()))
    {
        return false;
    }

    // The header and the JSON name the same parameters, with the same types.
    const TSharedPtr<FJsonObject> JsonParameters = Contract->GetObjectField(TEXT("parameters"));
    TestEqual(TEXT("the JSON has exactly the header's parameters"), JsonParameters->Values.Num(), Roles().Num());
    for (const FRole& Role : Roles())
    {
        const TSharedPtr<FJsonObject>* Parameter = nullptr;
        if (!TestTrue(FString::Printf(TEXT("the JSON has %s"), Role.Key), JsonParameters->TryGetObjectField(Role.Key, Parameter)))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("%s is named alike on both sides"), Role.Key),
            FName(*(*Parameter)->GetStringField(TEXT("name"))), Role.Name);
        TestEqual(FString::Printf(TEXT("%s has the same type on both sides"), Role.Key),
            (*Parameter)->GetStringField(TEXT("type")), FString(Role.Type));
    }
    TestEqual(TEXT("the disc gain is the one SkyProjection assumes"),
        Contract->GetObjectField(TEXT("constants"))->GetNumberField(TEXT("lambert_disc_gain")), SkyMaterial::LambertDiscGain);

    struct FExpected
    {
        const TCHAR* Asset;
        const TCHAR* ObjectPath;
        TArray<FName> Scalars;
        TArray<FName> Vectors;
    };
    const TArray<FExpected> Materials = {
        { TEXT("M_SkyBody"), SkyMaterial::BodyPath, SkyMaterial::BodyScalars(), SkyMaterial::BodyVectors() },
        { TEXT("M_SkyStar"), SkyMaterial::StarPath, SkyMaterial::StarScalars(), SkyMaterial::StarVectors() },
        { TEXT("M_SkyStarfield"), SkyMaterial::StarfieldPath, {}, {} },
        { TEXT("M_SkyGlass"), SkyMaterial::GlassPath, {}, {} },
    };

    const TSharedPtr<FJsonObject> JsonMaterials = Contract->GetObjectField(TEXT("materials"));
    const FString Directory = Contract->GetStringField(TEXT("directory"));
    TestEqual(TEXT("the JSON lists exactly the header's materials"), JsonMaterials->Values.Num(), Materials.Num());

    for (const FExpected& Expected : Materials)
    {
        TestEqual(FString::Printf(TEXT("%s lives where the JSON says"), Expected.Asset), FString(Expected.ObjectPath),
            FString::Printf(TEXT("%s/%s.%s"), *Directory, Expected.Asset, Expected.Asset));

        const TSharedPtr<FJsonObject>* Entry = nullptr;
        if (!TestTrue(FString::Printf(TEXT("the JSON has %s"), Expected.Asset), JsonMaterials->TryGetObjectField(Expected.Asset, Entry)))
        {
            continue;
        }
        TSet<FName> JsonScalars;
        TSet<FName> JsonVectors;
        JsonNames(Contract, *Entry, JsonScalars, JsonVectors);
        TestTrue(FString::Printf(TEXT("%s: the JSON's scalars %s are the header's"), Expected.Asset, *Describe(JsonScalars)),
            SameSet(JsonScalars, TSet<FName>(Expected.Scalars)));
        TestTrue(FString::Printf(TEXT("%s: the JSON's vectors %s are the header's"), Expected.Asset, *Describe(JsonVectors)),
            SameSet(JsonVectors, TSet<FName>(Expected.Vectors)));

        // The built asset exposes exactly those. A misspelt parameter is a
        // silent no-op at runtime; here it is a red test.
        const UMaterial* Material = LoadObject<UMaterial>(nullptr, Expected.ObjectPath);
        if (!TestNotNull(FString::Printf(TEXT("%s is built (Tools/setup_sky_materials.py)"), Expected.Asset), Material))
        {
            continue;
        }
        const TSet<FName> Scalars = AssetNames(Material, true);
        const TSet<FName> Vectors = AssetNames(Material, false);
        TestTrue(FString::Printf(TEXT("%s exposes exactly the contract's scalars; has %s"), Expected.Asset, *Describe(Scalars)),
            SameSet(Scalars, JsonScalars));
        TestTrue(FString::Printf(TEXT("%s exposes exactly the contract's vectors; has %s"), Expected.Asset, *Describe(Vectors)),
            SameSet(Vectors, JsonVectors));
        // One node per parameter. Two nodes can share a name and the name
        // lists above cannot tell; a stale one left by a rebuild is how that
        // happens, and whichever the compiler picks is a guess.
        int32 ParameterNodes = 0;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
        {
            ParameterNodes += Cast<UMaterialExpressionParameter>(Expression) ? 1 : 0;
        }
        TestEqual(FString::Printf(TEXT("%s has one node per parameter"), Expected.Asset),
            ParameterNodes, JsonScalars.Num() + JsonVectors.Num());

        const TArray<FString> Errors = TranslationErrors(const_cast<UMaterial*>(Material));
        TestTrue(FString::Printf(TEXT("%s translates: %s"), Expected.Asset, *FString::Join(Errors, TEXT("; "))), Errors.IsEmpty());
        TestTrue(FString::Printf(TEXT("%s is unlit: its emissive is the final pixel"), Expected.Asset),
            Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));

        if (Material->GetFName() == TEXT("M_SkyGlass"))
        {
            TestTrue(TEXT("M_SkyGlass is translucent"), Material->GetBlendMode() == BLEND_Translucent);
        }
        if (Material->GetFName() == TEXT("M_SkyStarfield"))
        {
            // Editor-only data, present under UnrealEditor-Cmd.
            TSet<int32> Indices;
            for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
            {
                if (const UMaterialExpressionPerInstanceCustomData* Custom = Cast<UMaterialExpressionPerInstanceCustomData>(Expression))
                {
                    Indices.Add(static_cast<int32>(Custom->DataIndex));
                }
            }
            TestEqual(TEXT("the JSON's custom-data count is the header's"),
                static_cast<int32>((*Entry)->GetNumberField(TEXT("custom_data"))), SkyMaterial::StarfieldCustomData);
            for (const int32 Index : { SkyMaterial::CustomDataRed, SkyMaterial::CustomDataGreen,
                                       SkyMaterial::CustomDataBlue, SkyMaterial::CustomDataBrightness })
            {
                TestTrue(FString::Printf(TEXT("M_SkyStarfield reads PerInstanceCustomData %d"), Index), Indices.Contains(Index));
            }
            TestEqual(TEXT("and nothing else"), Indices.Num(), SkyMaterial::StarfieldCustomData);
            TestTrue(TEXT("M_SkyStarfield may be used on instanced meshes"), Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        }
        if (Material->GetFName() == TEXT("M_SkyStar"))
        {
            TestTrue(TEXT("M_SkyStar may be used on instanced meshes (the motes)"), Material->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
        }
    }

    // MPC_Sky: the header, the JSON and the asset hold the same scalars, and
    // M_SkyGlass reads exactly those -- by an id that still resolves.
    {
        const TSharedPtr<FJsonObject> Collection = Contract->GetObjectField(TEXT("collections"))->GetObjectField(TEXT("MPC_Sky"));
        TSet<FName> JsonScalars;
        TSet<FName> JsonVectors;
        JsonNames(Contract, Collection, JsonScalars, JsonVectors);
        TestTrue(TEXT("MPC_Sky: the JSON's scalars are the header's"), SameSet(JsonScalars, TSet<FName>(SkyMaterial::ParameterScalars())));
        TestEqual(TEXT("MPC_Sky has no vectors"), JsonVectors.Num(), 0);
        TestEqual(TEXT("MPC_Sky lives where the JSON says"), FString(SkyMaterial::ParametersPath),
            FString::Printf(TEXT("%s/MPC_Sky.MPC_Sky"), *Directory));
        TestFalse(TEXT("MPC_Sky is no longer pending: the veil has landed"), Collection->HasField(TEXT("pending")));

        const UMaterialParameterCollection* Built = LoadObject<UMaterialParameterCollection>(nullptr, SkyMaterial::ParametersPath);
        if (TestNotNull(TEXT("MPC_Sky is built (Tools/setup_sky_materials.py)"), Built))
        {
            TSet<FName> Names;
            for (const FCollectionScalarParameter& Parameter : Built->ScalarParameters)
            {
                Names.Add(Parameter.ParameterName);
            }
            TestTrue(TEXT("MPC_Sky holds exactly its scalars; has ") + Describe(Names), SameSet(Names, JsonScalars));
            TestEqual(TEXT("and no vectors"), Built->VectorParameters.Num(), 0);

            // The defaults are what the glass shows where nothing writes
            // them: the editor's viewport, before play.
            const TSharedPtr<FJsonObject> Defaults = Collection->GetObjectField(TEXT("defaults"));
            const TSharedPtr<FJsonObject> ByRole = Contract->GetObjectField(TEXT("parameters"));
            for (const TSharedPtr<FJsonValue>& Role : Collection->GetArrayField(TEXT("parameters")))
            {
                const FName Name(*ByRole->GetObjectField(Role->AsString())->GetStringField(TEXT("name")));
                const FCollectionScalarParameter* Parameter = Built->ScalarParameters.FindByPredicate(
                    [&Name](const FCollectionScalarParameter& Candidate) { return Candidate.ParameterName == Name; });
                TestTrue(FString::Printf(TEXT("MPC_Sky's %s defaults to the JSON's"), *Name.ToString()),
                    Parameter && FMath::IsNearlyEqual(Parameter->DefaultValue, static_cast<float>(Defaults->GetNumberField(Role->AsString()))));
            }

            // A material holds a collection parameter by id and looks its
            // name up on load. An id the collection no longer has loads as
            // None and reads zero: a veil that is simply never there, with
            // the names above all correct.
            const UMaterial* Glass = LoadObject<UMaterial>(nullptr, SkyMaterial::GlassPath);
            if (TestNotNull(TEXT("M_SkyGlass is built"), Glass))
            {
                TSet<FName> Read;
                int32 Nodes = 0;
                for (const TObjectPtr<UMaterialExpression>& Expression : Glass->GetExpressions())
                {
                    const UMaterialExpressionCollectionParameter* Node = Cast<UMaterialExpressionCollectionParameter>(Expression);
                    if (!Node)
                    {
                        continue;
                    }
                    ++Nodes;
                    Read.Add(Node->ParameterName);
                    TestTrue(FString::Printf(TEXT("M_SkyGlass reads %s from MPC_Sky"), *Node->ParameterName.ToString()),
                        Node->Collection == Built);
                    TestEqual(FString::Printf(TEXT("M_SkyGlass's %s is held by an id MPC_Sky still has"), *Node->ParameterName.ToString()),
                        Built->GetParameterName(Node->ParameterId), Node->ParameterName);
                }
                TestTrue(TEXT("M_SkyGlass reads exactly MPC_Sky's scalars, the veil and the room's light; reads ") + Describe(Read),
                    SameSet(Read, JsonScalars));
                TestEqual(TEXT("once each"), Nodes, JsonScalars.Num());
            }
        }

        // The veil's tuning target, stated so it can be checked (sky decision
        // 6): a fully lit room reflects in the glass at about the pixel value
        // of a flux-4 star, which leaves the brightest eighth of the starfield
        // showing through it. Translucency blends the glass's emissive in at
        // its opacity, so that is what reaches the screen.
        const TSharedPtr<FJsonObject> Constants = Contract->GetObjectField(TEXT("constants"));
        const TArray<TSharedPtr<FJsonValue>>& VeilRgb = Constants->GetArrayField(TEXT("veil_colour"));
        if (TestEqual(TEXT("the veil is a colour"), VeilRgb.Num(), 3))
        {
            const FLinearColor VeilColour(VeilRgb[0]->AsNumber(), VeilRgb[1]->AsNumber(), VeilRgb[2]->AsNumber());
            const IConsoleVariable* Veil = IConsoleManager::Get().FindConsoleVariable(TEXT("ds.Sky.Veil"));
            const double OnScreen = VeilColour.GetLuminance() * Constants->GetNumberField(TEXT("glass_opacity"))
                * (Veil ? Veil->GetFloat() : 0.0);
            const double FluxFour = AShipSky::PointStarBrightness(4.0);
            TestTrue(FString::Printf(TEXT("a lit room's veil (%.4f on screen) is about a flux-4 star (%.4f)"), OnScreen, FluxFour),
                FMath::Abs(OnScreen / FluxFour - 1.0) < 0.15);
        }
    }
    return true;
}

#endif
