#include "Misc/AutomationTest.h"
#include "Algo/Compare.h"
#include "Dom/JsonObject.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionDDX.h"
#include "Materials/MaterialExpressionDDY.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionVectorNoise.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionViewSize.h"
#include "Materials/MaterialExpressionWorldPosition.h"
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
#include "Materials/MaterialExpressionCustom.h"
#include "Surface/WorldRelief.h"

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
            { TEXT("detail"), SkyMaterial::Detail, TEXT("scalar") },
            { TEXT("banding"), SkyMaterial::Banding, TEXT("scalar") },
            { TEXT("relief_scale"), SkyMaterial::ReliefScale, TEXT("scalar") },
            { TEXT("cratering"), SkyMaterial::Cratering, TEXT("scalar") },
            { TEXT("surface_seed"), SkyMaterial::SurfaceSeed, TEXT("vector") },
            { TEXT("body_axis_x"), SkyMaterial::BodyAxisX, TEXT("vector") },
            { TEXT("body_axis_y"), SkyMaterial::BodyAxisY, TEXT("vector") },
            { TEXT("probe_footprint"), SkyMaterial::ProbeFootprint, TEXT("scalar") },
            { TEXT("probe_select"), SkyMaterial::ProbeSelect, TEXT("vector") },
            { TEXT("probe_bias"), SkyMaterial::ProbeBias, TEXT("vector") },
            { TEXT("interior_light"), SkyMaterial::InteriorLight, TEXT("scalar") },
            { TEXT("veil"), SkyMaterial::Veil, TEXT("scalar") },
            { TEXT("morph"), SkyMaterial::Morph, TEXT("scalar") },
            { TEXT("band_limit"), SkyMaterial::BandLimit, TEXT("scalar") },
            { TEXT("tile_pivot"), SkyMaterial::TilePivot, TEXT("vector") },
            { TEXT("vertex_band_limit"), SkyMaterial::VertexBandLimit, TEXT("scalar") },
            { TEXT("shadows"), SkyMaterial::Shadows, TEXT("scalar") },
            { TEXT("shadow_map"), SkyMaterial::ShadowMap, TEXT("texture") },
            { TEXT("shadow_frame_x"), SkyMaterial::ShadowFrameX, TEXT("vector") },
            { TEXT("shadow_frame_z"), SkyMaterial::ShadowFrameZ, TEXT("vector") },
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
                   TSet<FName>& OutScalars, TSet<FName>& OutVectors, TSet<FName>& OutTextures)
    {
        const TSharedPtr<FJsonObject> Parameters = Contract->GetObjectField(TEXT("parameters"));
        for (const TSharedPtr<FJsonValue>& Role : Entry->GetArrayField(TEXT("parameters")))
        {
            const TSharedPtr<FJsonObject> Parameter = Parameters->GetObjectField(Role->AsString());
            const FName Name(*Parameter->GetStringField(TEXT("name")));
            const FString Type = Parameter->GetStringField(TEXT("type"));
            (Type == TEXT("scalar") ? OutScalars : (Type == TEXT("texture") ? OutTextures : OutVectors)).Add(Name);
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

    /** Every expression feeding From, From included, walking back through
     *  every input -- except that a node in Stops, when reached, is collected
     *  but not entered, so what lies only behind the stops is left out. */
    TSet<const UMaterialExpression*> Upstream(const UMaterialExpression* From, const TSet<const UMaterialExpression*>& Stops = {})
    {
        TSet<const UMaterialExpression*> Seen;
        TArray<const UMaterialExpression*> Open;
        if (From)
        {
            Open.Add(From);
        }
        while (Open.Num() > 0)
        {
            const UMaterialExpression* Node = Open.Pop();
            bool bAlready = false;
            Seen.Add(Node, &bAlready);
            if (bAlready || Stops.Contains(Node))
            {
                continue;
            }
            for (int32 Index = 0; const FExpressionInput* Input = Node->GetInput(Index); ++Index)
            {
                if (Input->Expression)
                {
                    Open.Add(Input->Expression);
                }
            }
        }
        return Seen;
    }

    /** Every node that takes Of as an input. */
    TArray<const UMaterialExpression*> Consumers(const UMaterial& Material, const UMaterialExpression* Of)
    {
        TArray<const UMaterialExpression*> Found;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            for (int32 Index = 0; const FExpressionInput* Input = Expression->GetInput(Index); ++Index)
            {
                if (Input->Expression == Of)
                {
                    Found.Add(Expression.Get());
                }
            }
        }
        return Found;
    }

    /** The shared file as a material reaches it (landing decision 1): one
     *  Custom node that includes WorldRelief.ush and calls its entry point,
     *  its pins the file's, and no engine noise anywhere -- a band left on an
     *  engine node is a band the C++ does not have. Returns the node. */
    const UMaterialExpressionCustom* CheckSharedRelief(FAutomationTestBase& Test, const UMaterial& Material)
    {
        const FString Asset = Material.GetName();
        TArray<const UMaterialExpressionCustom*> Customs;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            // The cast shadow's node includes the same file for its map's
            // lookup (CheckShadowNode holds it); the face's is the other.
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression.Get());
            if (Custom && !Custom->Code.Contains(SkyMaterial::ShadowCoordEntry))
            {
                Customs.Add(Custom);
            }
            const bool bEngineNoise = Cast<UMaterialExpressionNoise>(Expression.Get()) || Cast<UMaterialExpressionVectorNoise>(Expression.Get());
            Test.TestFalse(FString::Printf(TEXT("%s draws no band on an engine noise node (%s)"), *Asset, *Expression->GetName()), bEngineNoise);
        }
        if (!Test.TestEqual(FString::Printf(TEXT("%s reaches the shared file through one Custom node"), *Asset), Customs.Num(), 1))
        {
            return nullptr;
        }
        const UMaterialExpressionCustom* Node = Customs[0];
        Test.TestTrue(FString::Printf(TEXT("%s's Custom node includes %s, and only it"), *Asset, SkyMaterial::WorldReliefInclude),
            Node->IncludeFilePaths.Num() == 1 && Node->IncludeFilePaths[0] == SkyMaterial::WorldReliefInclude);
        Test.TestTrue(FString::Printf(TEXT("and calls %s"), SkyMaterial::WorldReliefEntry),
            Node->Code.Contains(FString(SkyMaterial::WorldReliefEntry) + TEXT("(")));
        TArray<FName> Inputs;
        for (const FCustomInput& Input : Node->Inputs)
        {
            Inputs.Add(Input.InputName);
            Test.TestNotNull(FString::Printf(TEXT("%s's %s is wired"), *Asset, *Input.InputName.ToString()), Input.Input.Expression);
        }
        Test.TestTrue(TEXT("its inputs are the file's, in order"), Inputs == SkyMaterial::WorldReliefInputs());
        TArray<FName> Outputs;
        for (const FCustomOutput& Output : Node->AdditionalOutputs)
        {
            Outputs.Add(Output.OutputName);
        }
        Test.TestTrue(TEXT("and so are its outputs"), Outputs == SkyMaterial::WorldReliefOutputs());
        return Node;
    }

    /** The JSON's shared_relief and band constants against the header and
     *  against the shared file's own tables: the file is the source, the
     *  JSON the list the Custom-node graphs and the docs are built from. */
    void CheckSharedTables(FAutomationTestBase& Test, const TSharedPtr<FJsonObject>& Contract)
    {
        const TSharedPtr<FJsonObject> Shared = Contract->GetObjectField(TEXT("shared_relief"));
        Test.TestEqual(TEXT("the JSON's include is the header's"), Shared->GetStringField(TEXT("include")), FString(SkyMaterial::WorldReliefInclude));
        Test.TestEqual(TEXT("and its entry point"), Shared->GetStringField(TEXT("entry")), FString(SkyMaterial::WorldReliefEntry));
        TArray<FName> Inputs;
        for (const TSharedPtr<FJsonValue>& Value : Shared->GetArrayField(TEXT("inputs")))
        {
            Inputs.Add(FName(*Value->AsString()));
        }
        Test.TestTrue(TEXT("and its inputs"), Inputs == SkyMaterial::WorldReliefInputs());
        TArray<FName> Outputs;
        for (const TSharedPtr<FJsonValue>& Value : Shared->GetArrayField(TEXT("outputs")))
        {
            Outputs.Add(FName(*Value->AsArray()[0]->AsString()));
        }
        Test.TestTrue(TEXT("and its outputs"), Outputs == SkyMaterial::WorldReliefOutputs());

        const TSharedPtr<FJsonObject> Constants = Contract->GetObjectField(TEXT("constants"));
        const WorldReliefNoise::FBands Bands = WorldReliefNoise::Bands();
        Test.TestEqual(TEXT("the file's coarse band is the contract's"), Bands.ContinentFrequency, Constants->GetNumberField(TEXT("continent_frequency")));
        Test.TestEqual(TEXT("with its octaves"), Bands.ContinentLevels, static_cast<int32>(Constants->GetNumberField(TEXT("continent_levels"))));
        Test.TestEqual(TEXT("at its step"), Bands.LevelScale, Constants->GetNumberField(TEXT("level_scale")));

        TArray<int32> EveryDetail;
        for (int32 Number = 1; Number <= Constants->GetArrayField(TEXT("detail_frequencies")).Num(); ++Number)
        {
            EveryDetail.Add(Number);
        }
        TArray<int32> EveryCrater;
        for (int32 Number = 101; Number < 101 + Constants->GetArrayField(TEXT("crater_frequencies")).Num(); ++Number)
        {
            EveryCrater.Add(Number);
        }
        Test.TestTrue(TEXT("the shared file carries every detail band the contract has, in order"), Bands.DetailIndices == EveryDetail);
        Test.TestTrue(TEXT("and every crater band"), Bands.CraterIndices == EveryCrater);

        const TArray<TSharedPtr<FJsonValue>>& Frequencies = Constants->GetArrayField(TEXT("detail_frequencies"));
        const TArray<TSharedPtr<FJsonValue>>& Weights = Constants->GetArrayField(TEXT("detail_weights"));
        for (int32 Band = 0; Band < Bands.DetailIndices.Num(); ++Band)
        {
            const int32 Number = Bands.DetailIndices[Band];
            if (Test.TestTrue(FString::Printf(TEXT("detail band %d is one the contract has"), Number), Frequencies.IsValidIndex(Number - 1)))
            {
                Test.TestEqual(FString::Printf(TEXT("detail band %d's frequency"), Number), Bands.DetailFrequencies[Band], Frequencies[Number - 1]->AsNumber());
                Test.TestEqual(FString::Printf(TEXT("detail band %d's weight"), Number), Bands.DetailWeights[Band], Weights[Number - 1]->AsNumber());
            }
        }
        const TArray<TSharedPtr<FJsonValue>>& CraterFrequencies = Constants->GetArrayField(TEXT("crater_frequencies"));
        for (int32 Band = 0; Band < Bands.CraterIndices.Num(); ++Band)
        {
            const int32 Number = Bands.CraterIndices[Band];
            if (Test.TestTrue(FString::Printf(TEXT("crater band %d is one the contract has"), Number), CraterFrequencies.IsValidIndex(Number - 101)))
            {
                Test.TestEqual(FString::Printf(TEXT("crater band %d's frequency"), Number), Bands.CraterFrequencies[Band], CraterFrequencies[Number - 101]->AsNumber());
            }
        }
        const double Radius = Constants->GetNumberField(TEXT("crater_radius"));
        const double Depth = Constants->GetNumberField(TEXT("crater_depth"));
        const double Rim = Constants->GetNumberField(TEXT("crater_rim"));
        Test.TestEqual(TEXT("a crater's radius"), Bands.CraterRadius, Radius);
        Test.TestEqual(TEXT("its reciprocal, as the graph computed it"), Bands.CraterInvRadius, 1.0 / Radius);
        Test.TestEqual(TEXT("its bowl's slope, 2 x depth"), Bands.CraterWall, 2.0 * Depth);
        Test.TestEqual(TEXT("its rim's fall, -4 x depth x rim"), Bands.CraterRimFall, -4.0 * Depth * Rim);
        Test.TestEqual(TEXT("the share of sites kept"), Bands.CraterKeep, Constants->GetNumberField(TEXT("crater_keep")));
        Test.TestEqual(TEXT("the floor's darkening"), Bands.CraterFloorDark, Constants->GetNumberField(TEXT("crater_floor_dark")));
        Test.TestEqual(TEXT("the rim's brightening"), Bands.CraterRimBright, Constants->GetNumberField(TEXT("crater_rim_bright")));
    }

    /**
     * A little evaluator for the vector arithmetic of a material graph: the
     * nodes body_axes, to_body and to_world are built from, and nothing
     * else. A value is its components, 1 to 4. Given is what the caller
     * sets -- a node's value outright, or a vector parameter's by name --
     * and a node it cannot evaluate fails the evaluation, so a graph that
     * reaches the result some other way is refused, never guessed at.
     */
    struct FGraphEval
    {
        TMap<const UMaterialExpression*, TArray<double>> Given;
        TMap<FName, TArray<double>> Vectors;
        FString Failure;

        TArray<double> Input(const FExpressionInput& In, TArray<double> Unlinked = {})
        {
            if (!In.Expression)
            {
                return Unlinked;
            }
            TArray<double> Value = Node(In.Expression);
            if (In.Mask && Value.Num() > 0)
            {
                TArray<double> Masked;
                const int32 Flags[4] = { In.MaskR, In.MaskG, In.MaskB, In.MaskA };
                for (int32 Channel = 0; Channel < 4; ++Channel)
                {
                    if (Flags[Channel] && Value.IsValidIndex(Channel))
                    {
                        Masked.Add(Value[Channel]);
                    }
                }
                return Masked;
            }
            return Value;
        }

        /** A op B, a one-component operand broadcast across the other. */
        template <typename FOp>
        TArray<double> Each(const TArray<double>& A, const TArray<double>& B, FOp Op)
        {
            if (A.IsEmpty() || B.IsEmpty() || (A.Num() != B.Num() && A.Num() != 1 && B.Num() != 1))
            {
                Failure = TEXT("operands of mismatched width");
                return {};
            }
            TArray<double> Out;
            for (int32 Index = 0; Index < FMath::Max(A.Num(), B.Num()); ++Index)
            {
                Out.Add(Op(A[A.Num() == 1 ? 0 : Index], B[B.Num() == 1 ? 0 : Index]));
            }
            return Out;
        }

        static double Dot(const TArray<double>& A, const TArray<double>& B)
        {
            double Sum = 0.0;
            for (int32 Index = 0; Index < FMath::Min(A.Num(), B.Num()); ++Index)
            {
                Sum += A[Index] * B[Index];
            }
            return Sum;
        }

        TArray<double> Node(const UMaterialExpression* E)
        {
            if (const TArray<double>* Set = Given.Find(E))
            {
                return *Set;
            }
            if (const UMaterialExpressionVectorParameter* P = Cast<UMaterialExpressionVectorParameter>(E))
            {
                if (const TArray<double>* Set = Vectors.Find(P->ParameterName))
                {
                    return *Set;
                }
            }
            else if (const UMaterialExpressionNormalize* N = Cast<UMaterialExpressionNormalize>(E))
            {
                TArray<double> V = Input(N->VectorInput);
                const double Length = FMath::Sqrt(Dot(V, V));
                for (double& C : V)
                {
                    C /= Length;
                }
                return V;
            }
            else if (const UMaterialExpressionDotProduct* D = Cast<UMaterialExpressionDotProduct>(E))
            {
                const TArray<double> A = Input(D->A), B = Input(D->B);
                if (A.Num() == B.Num() && !A.IsEmpty())
                {
                    return { Dot(A, B) };
                }
            }
            else if (const UMaterialExpressionCrossProduct* X = Cast<UMaterialExpressionCrossProduct>(E))
            {
                const TArray<double> A = Input(X->A), B = Input(X->B);
                if (A.Num() == 3 && B.Num() == 3)
                {
                    return { A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0] };
                }
            }
            else if (const UMaterialExpressionAppendVector* V = Cast<UMaterialExpressionAppendVector>(E))
            {
                TArray<double> Out = Input(V->A);
                Out.Append(Input(V->B));
                return Out;
            }
            else if (const UMaterialExpressionComponentMask* M = Cast<UMaterialExpressionComponentMask>(E))
            {
                const TArray<double> In = Input(M->Input);
                TArray<double> Out;
                const bool Flags[4] = { !!M->R, !!M->G, !!M->B, !!M->A };
                for (int32 Channel = 0; Channel < 4; ++Channel)
                {
                    if (Flags[Channel] && In.IsValidIndex(Channel))
                    {
                        Out.Add(In[Channel]);
                    }
                }
                return Out;
            }
            else if (const UMaterialExpressionMultiply* Mul = Cast<UMaterialExpressionMultiply>(E))
            {
                return Each(Input(Mul->A, { Mul->ConstA }), Input(Mul->B, { Mul->ConstB }), [](double A, double B) { return A * B; });
            }
            else if (const UMaterialExpressionAdd* Add = Cast<UMaterialExpressionAdd>(E))
            {
                return Each(Input(Add->A, { Add->ConstA }), Input(Add->B, { Add->ConstB }), [](double A, double B) { return A + B; });
            }
            else if (const UMaterialExpressionSubtract* Sub = Cast<UMaterialExpressionSubtract>(E))
            {
                return Each(Input(Sub->A, { Sub->ConstA }), Input(Sub->B, { Sub->ConstB }), [](double A, double B) { return A - B; });
            }
            if (Failure.IsEmpty())
            {
                Failure = FString::Printf(TEXT("reached %s, which the check cannot evaluate"), *E->GetClass()->GetName());
            }
            return {};
        }
    };

    FVector AsVector(const TArray<double>& V)
    {
        return V.Num() == 3 ? FVector(V[0], V[1], V[2]) : FVector(NAN);
    }

    /**
     * M_SkyBody turns the face with the ship. The proxy is drawn unturned,
     * so the mesh's object space has the world's axes, and the material
     * alone carries the body's: the direction the noise reads must be the
     * pixel's world direction in the axes C++ writes to BodyAxisX/BodyAxisY
     * (Z their cross product), and the relief's normal must come back from
     * those axes to the world, where the light is. The names above cannot
     * see that, and a graph that skips the turn, or turns the wrong way,
     * draws a face fixed to the world, which swims as the ship turns. So
     * the arithmetic is evaluated, under a turn with no symmetry to hide
     * a transposition or a swapped axis:
     *
     * - the direction feeding the footprint (DDX) and every band is
     *   (X . p, Y . p, (X x Y) . p) for p = normalize(LocalPosition);
     * - LocalPosition reaches the pixel only through that direction;
     * - the normal the light is dotted with is normalize(v.x X + v.y Y +
     *   v.z (X x Y)) for the relief's body-space normal v.
     */
    void CheckBodyTurn(FAutomationTestBase& Test, UMaterial& Material)
    {
        const UMaterialExpressionLocalPosition* Position = nullptr;
        const UMaterialExpressionDDX* Footprint = nullptr;
        const UMaterialExpressionVectorParameter* Light = nullptr;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            Position = Position ? Position : Cast<UMaterialExpressionLocalPosition>(Expression.Get());
            Footprint = Footprint ? Footprint : Cast<UMaterialExpressionDDX>(Expression.Get());
            const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression.Get());
            Light = Vector && Vector->ParameterName == SkyMaterial::LightDirection ? Vector : Light;
        }
        const UMaterialExpression* Direction = Footprint ? Footprint->Value.Expression : nullptr;
        if (!Test.TestNotNull(TEXT("M_SkyBody reads the mesh's position"), Position)
            || !Test.TestNotNull(TEXT("M_SkyBody's footprint is taken of the body's direction"), Direction)
            || !Test.TestNotNull(TEXT("M_SkyBody has its light direction"), Light))
        {
            return;
        }

        // A turn about an oblique axis, and a pixel off every axis.
        const FQuat Turn(FVector(0.3, -0.7, 0.2).GetSafeNormal(), 0.83);
        const FVector X = Turn.GetAxisX(), Y = Turn.GetAxisY(), Z = X.Cross(Y);
        const FVector Pixel(0.31, -0.52, 0.8);
        const FVector Seen = Pixel.GetSafeNormal();

        FGraphEval Eval;
        Eval.Vectors.Add(SkyMaterial::BodyAxisX, { X.X, X.Y, X.Z, 0.0 });
        Eval.Vectors.Add(SkyMaterial::BodyAxisY, { Y.X, Y.Y, Y.Z, 0.0 });
        Eval.Given.Add(Position, { Pixel.X, Pixel.Y, Pixel.Z, 1.0 });
        const FVector Body = AsVector(Eval.Node(Direction));
        const FVector Expected(X.Dot(Seen), Y.Dot(Seen), Z.Dot(Seen));
        Test.TestTrue(FString::Printf(TEXT("M_SkyBody's face is read in the body's axes, turned by BodyAxisX/Y: %s, not %s%s"),
            *Body.ToString(), *Expected.ToString(), Eval.Failure.IsEmpty() ? TEXT("") : *(TEXT(" -- ") + Eval.Failure)),
            Body.Equals(Expected, 1e-6));

        // No band reads the position except through that turn.
        const FExpressionInput* Emissive = Material.GetExpressionInputForProperty(MP_EmissiveColor);
        const TSet<const UMaterialExpression*> Around = Upstream(Emissive ? Emissive->Expression : nullptr, { Direction });
        Test.TestFalse(TEXT("M_SkyBody's pixel reads the mesh's position only through the body's axes"), Around.Contains(Position));

        // The relief's normal, from the body's axes back to the world: the
        // node the light is dotted with, given the body-space normal.
        const UMaterialExpression* Normal = nullptr;
        for (const UMaterialExpression* User : Consumers(Material, Light))
        {
            if (const UMaterialExpressionDotProduct* Lit = Cast<UMaterialExpressionDotProduct>(User))
            {
                const UMaterialExpression* Other = Lit->A.Expression == Light ? Lit->B.Expression : Lit->A.Expression;
                Normal = Cast<UMaterialExpressionNormalize>(Other) ? Other : Normal;
            }
        }
        const UMaterialExpression* Relief = nullptr;
        for (const UMaterialExpression* User : Consumers(Material, Direction))
        {
            const UMaterialExpressionSubtract* Tilt = Cast<UMaterialExpressionSubtract>(User);
            if (Tilt && Tilt->A.Expression == Direction)
            {
                const TArray<const UMaterialExpression*> Users = Consumers(Material, Tilt);
                Relief = Users.Num() == 1 && Cast<UMaterialExpressionNormalize>(Users[0]) ? Users[0] : Relief;
            }
        }
        if (!Test.TestNotNull(TEXT("M_SkyBody lights a unit normal"), Normal)
            || !Test.TestNotNull(TEXT("M_SkyBody's relief tilts the body's direction"), Relief))
        {
            return;
        }
        const FVector Tilted = FVector(0.2, 0.9, -0.38).GetSafeNormal();
        FGraphEval Back;
        Back.Vectors = Eval.Vectors;
        Back.Given.Add(Relief, { Tilted.X, Tilted.Y, Tilted.Z });
        const FVector World = AsVector(Back.Node(Normal));
        const FVector WorldExpected = (X * Tilted.X + Y * Tilted.Y + Z * Tilted.Z).GetSafeNormal();
        Test.TestTrue(FString::Printf(TEXT("M_SkyBody's relief normal comes back to the world through the same axes: %s, not %s%s"),
            *World.ToString(), *WorldExpected.ToString(), Back.Failure.IsEmpty() ? TEXT("") : *(TEXT(" -- ") + Back.Failure)),
            World.Equals(WorldExpected, 1e-6));
    }

    /**
     * M_SkyBody's face and relief, as the developer asked for them: detail
     * fixed to the body, in several bands, whose finer ones arrive as the
     * world grows, and ground that tilts where the light is low. The names
     * cannot see any of that, so the graph is read:
     *
     * - Object space and nothing else, and no texture (unchanged).
     * - The contract's bands, on their own terms: finer each time, no octave
     *   skipped, weights never falling, craters stepping by four.
     * - One shared file: every band is WorldRelief.ush's, through one Custom
     *   node (landing decision 1), handed D in the body's axes, the pixel's
     *   footprint, the world's seed and the banding's stretch.
     * - The half-float guard: the file's terms reach the pixel only through
     *   the clamp to +/- surface_max_swing and a unit normal.
     * - Every parameter reaches the pixel.
     */
    void CheckSurfaceFace(FAutomationTestBase& Test, UMaterial& Material, const TSharedPtr<FJsonObject>& Constants)
    {
        int32 LocalPositions = 0;
        TMap<FName, const UMaterialExpression*> Parameters;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            const UMaterialExpression* Node = Expression.Get();
            LocalPositions += Cast<UMaterialExpressionLocalPosition>(Node) ? 1 : 0;
            if (const UMaterialExpressionParameter* Parameter = Cast<UMaterialExpressionParameter>(Node))
            {
                Parameters.Add(Parameter->ParameterName, Parameter);
            }
            const bool bSwims = Cast<UMaterialExpressionWorldPosition>(Node) || Cast<UMaterialExpressionScreenPosition>(Node)
                || Cast<UMaterialExpressionPixelDepth>(Node) || Cast<UMaterialExpressionCameraPositionWS>(Node)
                || Cast<UMaterialExpressionObjectPositionWS>(Node) || Cast<UMaterialExpressionViewSize>(Node);
            Test.TestFalse(FString::Printf(TEXT("M_SkyBody's face reads no world, screen or camera position (%s)"), *Node->GetName()), bSwims);
            // The one texture M_SkyBody reads is its cast-shadow map, as an
            // object, and only the shadow's node takes it (CheckShadowNode);
            // nothing samples one into the face.
            const UMaterialExpressionTextureObjectParameter* Object = Cast<UMaterialExpressionTextureObjectParameter>(Node);
            Test.TestTrue(FString::Printf(TEXT("M_SkyBody reads no texture but its shadow map (%s)"), *Node->GetName()),
                Cast<UMaterialExpressionTextureBase>(Node) == nullptr || (Object && Object->ParameterName == SkyMaterial::ShadowMap));
        }
        Test.TestEqual(TEXT("M_SkyBody's face is taken from the mesh's own position, once"), LocalPositions, 1);

        // -- The contract's bands, on their own terms ---------------------------
        const TArray<TSharedPtr<FJsonValue>>& Frequencies = Constants->GetArrayField(TEXT("detail_frequencies"));
        const TArray<TSharedPtr<FJsonValue>>& Weights = Constants->GetArrayField(TEXT("detail_weights"));
        const TArray<TSharedPtr<FJsonValue>>& CraterFrequencies = Constants->GetArrayField(TEXT("crater_frequencies"));
        const double ContinentFrequency = Constants->GetNumberField(TEXT("continent_frequency"));
        const int32 ContinentLevels = static_cast<int32>(Constants->GetNumberField(TEXT("continent_levels")));
        const int32 DetailLevels = static_cast<int32>(Constants->GetNumberField(TEXT("detail_levels")));
        const double LevelScale = Constants->GetNumberField(TEXT("level_scale"));
        Test.TestEqual(TEXT("a weight for every detail band"), Weights.Num(), Frequencies.Num());
        Test.TestTrue(TEXT("several detail bands, not one"), Frequencies.Num() >= 2);
        Test.TestEqual(TEXT("a detail band is one octave: the vector noise has no others"), DetailLevels, 1);
        double Previous = ContinentFrequency;
        double Reach = ContinentFrequency * FMath::Pow(LevelScale, ContinentLevels);
        for (int32 Index = 0; Index < Frequencies.Num(); ++Index)
        {
            const double Frequency = Frequencies[Index]->AsNumber();
            Test.TestTrue(FString::Printf(TEXT("each detail band finer than the last (%g)"), Frequency), Frequency > Previous);
            Test.TestTrue(FString::Printf(TEXT("and no octave skipped before it (%g after a reach of %g)"), Frequency, Reach),
                Frequency <= Reach * (1.0 + 1e-9));
            Previous = Frequency;
            Reach = Frequency * FMath::Pow(LevelScale, DetailLevels);
            if (Index > 0 && Index < Weights.Num())
            {
                Test.TestTrue(FString::Printf(TEXT("a finer band is never weaker than a coarser (%g)"), Frequency),
                    Weights[Index]->AsNumber() >= Weights[Index - 1]->AsNumber());
            }
        }
        for (int32 Index = 1; Index < CraterFrequencies.Num(); ++Index)
        {
            Test.TestTrue(TEXT("crater bands step by four, so the count wider than D goes as D^-2"),
                FMath::IsNearlyEqual(CraterFrequencies[Index]->AsNumber() / CraterFrequencies[Index - 1]->AsNumber(), 4.0, 1e-9));
        }

        // -- One shared file, handed what the face is made of ---------------------
        const UMaterialExpressionCustom* Shared = CheckSharedRelief(Test, Material);
        if (!Shared)
        {
            return;
        }
        const auto Fed = [Shared](const TCHAR* Pin) -> TSet<const UMaterialExpression*>
        {
            for (const FCustomInput& Input : Shared->Inputs)
            {
                if (Input.InputName == FName(Pin))
                {
                    return Upstream(Input.Input.Expression);
                }
            }
            return {};
        };
        const auto HasA = [](const TSet<const UMaterialExpression*>& Nodes, TFunctionRef<bool(const UMaterialExpression*)> Is)
        {
            for (const UMaterialExpression* Node : Nodes)
            {
                if (Is(Node))
                {
                    return true;
                }
            }
            return false;
        };
        const auto Holds = [&Parameters](const TSet<const UMaterialExpression*>& Nodes, FName Name)
        {
            const UMaterialExpression* const* Found = Parameters.Find(Name);
            return Found && Nodes.Contains(*Found);
        };
        const TSet<const UMaterialExpression*> Direction = Fed(TEXT("Direction"));
        Test.TestTrue(TEXT("the file is handed D from the mesh's own position"),
            HasA(Direction, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionLocalPosition>(Node) != nullptr; }));
        Test.TestTrue(TEXT("turned into the body's axes by BodyAxisX and BodyAxisY"),
            Holds(Direction, SkyMaterial::BodyAxisX) && Holds(Direction, SkyMaterial::BodyAxisY));
        const TSet<const UMaterialExpression*> Footprint = Fed(TEXT("Footprint"));
        const double FilterPixels = Constants->GetNumberField(TEXT("filter_pixels"));
        Test.TestTrue(TEXT("its footprint is the pixel's: DDX and DDY of that D"),
            HasA(Footprint, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionDDX>(Node) != nullptr; })
            && HasA(Footprint, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionDDY>(Node) != nullptr; })
            && HasA(Footprint, [](const UMaterialExpression* Node) { return Cast<UMaterialExpressionLocalPosition>(Node) != nullptr; }));
        Test.TestTrue(TEXT("times filter_pixels"), HasA(Footprint, [FilterPixels](const UMaterialExpression* Node)
        {
            const UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Node);
            return Constant && FMath::IsNearlyEqual(static_cast<double>(Constant->R), FilterPixels, 1e-6);
        }));
        Test.TestTrue(TEXT("its offset is the world's SurfaceSeed"), Holds(Fed(TEXT("SeedOffset")), SkyMaterial::SurfaceSeed));
        Test.TestTrue(TEXT("its stretch is the Banding's"), Holds(Fed(TEXT("Stretch")), SkyMaterial::Banding));

        // -- The half-float guard, and every knob reaching the pixel -------------
        const FExpressionInput* Emissive = Material.GetExpressionInputForProperty(MP_EmissiveColor);
        const UMaterialExpression* Pixel = Emissive ? Emissive->Expression : nullptr;
        if (!Test.TestNotNull(TEXT("M_SkyBody's emissive is wired"), Pixel))
        {
            return;
        }
        const TSet<const UMaterialExpression*> Reached = Upstream(Pixel);
        const double Swing = Constants->GetNumberField(TEXT("surface_max_swing"));
        TArray<const UMaterialExpressionClamp*> Guards;
        for (const UMaterialExpression* Node : Reached)
        {
            const UMaterialExpressionClamp* Clamp = Cast<UMaterialExpressionClamp>(Node);
            if (Clamp && !Clamp->Min.Expression && !Clamp->Max.Expression
                && FMath::IsNearlyEqual(static_cast<double>(Clamp->MinDefault), -Swing, 1e-6)
                && FMath::IsNearlyEqual(static_cast<double>(Clamp->MaxDefault), Swing, 1e-6))
            {
                Guards.Add(Clamp);
            }
        }
        if (!Test.TestEqual(TEXT("the pixel is fed by one clamp to +/- surface_max_swing"), Guards.Num(), 1))
        {
            return;
        }
        const UMaterialExpressionClamp* Guard = Guards[0];

        // The light's dot products: each with a unit normal, so each is in
        // [-1, 1] whatever feeds the normal.
        const UMaterialExpression* const* Light = Parameters.Find(SkyMaterial::LightDirection);
        TSet<const UMaterialExpression*> Turns;
        for (const UMaterialExpression* Node : Reached)
        {
            const UMaterialExpressionDotProduct* Dot = Cast<UMaterialExpressionDotProduct>(Node);
            if (!Dot || !Light || (Dot->A.Expression != *Light && Dot->B.Expression != *Light))
            {
                continue;
            }
            const UMaterialExpression* Normal = Dot->A.Expression == *Light ? Dot->B.Expression : Dot->A.Expression;
            Test.TestTrue(TEXT("every N.L is of a unit normal"),
                Cast<UMaterialExpressionNormalize>(Normal) || Cast<UMaterialExpressionVertexNormalWS>(Normal));
            Turns.Add(Dot);
        }
        Test.TestTrue(TEXT("the light meets the relief's normal"), Turns.Num() >= 1);

        TSet<const UMaterialExpression*> Stops = Turns;
        Stops.Add(Guard);
        const TSet<const UMaterialExpression*> Unguarded = Upstream(Pixel, Stops);
        const TSet<const UMaterialExpression*> Guarded = Upstream(Guard->Input.Expression);
        TSet<const UMaterialExpression*> Turning;
        for (const UMaterialExpression* Dot : Turns)
        {
            Turning.Append(Upstream(Dot));
        }
        Test.TestTrue(TEXT("the shared file lies behind the guard"), Guarded.Contains(Shared));
        Test.TestFalse(TEXT("and reaches the pixel by no way but it and the unit normal"), Unguarded.Contains(Shared));
        Test.TestTrue(TEXT("the shared file tilts the normal: the relief is the same noise as the face"), Turning.Contains(Shared));
        for (const FName Knob : { SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::Banding, SkyMaterial::SurfaceSeed, SkyMaterial::Cratering })
        {
            const UMaterialExpression* const* Parameter = Parameters.Find(Knob);
            Test.TestTrue(FString::Printf(TEXT("%s shapes the face behind the guard"), *Knob.ToString()),
                Parameter && Guarded.Contains(*Parameter));
        }
        for (const FName Knob : { SkyMaterial::Mottle, SkyMaterial::Detail, SkyMaterial::ReliefScale, SkyMaterial::Cratering })
        {
            const UMaterialExpression* const* Parameter = Parameters.Find(Knob);
            Test.TestTrue(FString::Printf(TEXT("and %s reaches the pixel only through the guard or the unit normal"), *Knob.ToString()),
                Parameter && !Unguarded.Contains(*Parameter));
        }
        for (const FName Knob : { SkyMaterial::ReliefScale, SkyMaterial::Cratering })
        {
            const UMaterialExpression* const* Parameter = Parameters.Find(Knob);
            Test.TestTrue(FString::Printf(TEXT("%s tilts the normal"), *Knob.ToString()), Parameter && Turning.Contains(*Parameter));
        }
        for (const TPair<FName, const UMaterialExpression*>& Parameter : Parameters)
        {
            Test.TestTrue(FString::Printf(TEXT("%s reaches the pixel"), *Parameter.Key.ToString()), Reached.Contains(Parameter.Value));
        }
    }

    /**
     * The cast shadow's node, in a material that reads the map: exactly one
     * Custom node calls WR_ShadowMapCoord, through the shared file's include,
     * with the contract's pins in order; it alone takes the ShadowMap object;
     * the object's default is the white texture, so a world without a map
     * reads 1; and a face hands it one pixel's footprint.
     */
    void CheckShadowNode(FAutomationTestBase& Test, UMaterial& Material, const TSharedPtr<FJsonObject>& Contract)
    {
        const TSharedPtr<FJsonObject> Block = Contract->GetObjectField(TEXT("shadow"));
        Test.TestEqual(TEXT("the JSON's shadow entry is the header's"), Block->GetStringField(TEXT("entry")), FString(SkyMaterial::ShadowCoordEntry));
        Test.TestEqual(TEXT("and its default texture"), Block->GetStringField(TEXT("default_texture")), FString(SkyMaterial::ShadowDefaultTexturePath));
        TArray<FName> JsonInputs;
        for (const TSharedPtr<FJsonValue>& Value : Block->GetArrayField(TEXT("inputs")))
        {
            JsonInputs.Add(FName(*Value->AsString()));
        }
        Test.TestTrue(TEXT("and its pins"), JsonInputs == SkyMaterial::ShadowInputs());
        int32 Nodes = 0;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material.GetExpressions())
        {
            if (const UMaterialExpressionTextureObjectParameter* Object = Cast<UMaterialExpressionTextureObjectParameter>(Expression))
            {
                Test.TestTrue(FString::Printf(TEXT("%s: the map's default is the white texture"), *Material.GetName()),
                    Object->Texture && Object->Texture->GetPathName() == SkyMaterial::ShadowDefaultTexturePath);
            }
            const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression);
            if (!Custom)
            {
                continue;
            }
            bool bTakesMap = false;
            for (const FCustomInput& Input : Custom->Inputs)
            {
                bTakesMap = bTakesMap || Cast<UMaterialExpressionTextureObjectParameter>(Input.Input.Expression) != nullptr;
            }
            if (!Custom->Code.Contains(SkyMaterial::ShadowCoordEntry))
            {
                Test.TestFalse(FString::Printf(TEXT("%s: only the shadow's node takes the map (%s)"), *Material.GetName(), *Custom->GetName()), bTakesMap);
                continue;
            }
            ++Nodes;
            TArray<FName> Pins;
            for (const FCustomInput& Input : Custom->Inputs)
            {
                Pins.Add(Input.InputName);
            }
            Test.TestTrue(FString::Printf(TEXT("%s: the shadow's pins are the contract's"), *Material.GetName()), Pins == SkyMaterial::ShadowInputs());
            Test.TestTrue(FString::Printf(TEXT("%s: through the shared file"), *Material.GetName()),
                Custom->IncludeFilePaths.Contains(FString(SkyMaterial::WorldReliefInclude)));
            Test.TestTrue(FString::Printf(TEXT("%s: and it takes the map"), *Material.GetName()), bTakesMap);
            // The faces hand the node one pixel's footprint: theirs over
            // filter_pixels (face_footprint). Handed the filtered one, the
            // map would be read a level coarser than planned everywhere.
            if (Material.GetFName() != TEXT("M_SkyShadowProbe") && Custom->Inputs.Num() > 1)
            {
                const UMaterialExpressionMultiply* Over = Cast<UMaterialExpressionMultiply>(Custom->Inputs[1].Input.Expression);
                const UMaterialExpressionConstant* By = Over ? Cast<UMaterialExpressionConstant>(Over->B.Expression) : nullptr;
                const double FilterPixels = Contract->GetObjectField(TEXT("constants"))->GetNumberField(TEXT("filter_pixels"));
                Test.TestTrue(FString::Printf(TEXT("%s: the shadow's footprint is the face's over filter_pixels"), *Material.GetName()),
                    By && FMath::IsNearlyEqual(static_cast<double>(By->R), 1.0 / FilterPixels, 1e-6));
            }
        }
        Test.TestEqual(FString::Printf(TEXT("%s has one shadow node"), *Material.GetName()), Nodes, 1);
    }

    TSet<FName> AssetNames(const UMaterialInterface* Material, EMaterialParameterType Type)
    {
        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        switch (Type)
        {
        case EMaterialParameterType::Scalar: Material->GetAllScalarParameterInfo(Infos, Ids); break;
        case EMaterialParameterType::Vector: Material->GetAllVectorParameterInfo(Infos, Ids); break;
        default: Material->GetAllTextureParameterInfo(Infos, Ids); break;
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
    TestEqual(TEXT("the face's bound is the one the header promises"),
        Contract->GetObjectField(TEXT("constants"))->GetNumberField(TEXT("surface_max_swing")), SkyMaterial::SurfaceMaxSwing);
    TestTrue(TEXT("and it keeps a world between a tenth and twice its smooth disc: never black, never the ceiling's"),
        SkyMaterial::SurfaceMaxSwing > 0.0 && SkyMaterial::SurfaceMaxSwing < 1.0);

    struct FExpected
    {
        const TCHAR* Asset;
        const TCHAR* ObjectPath;
        TArray<FName> Scalars;
        TArray<FName> Vectors;
        TArray<FName> Textures;
    };
    const TArray<FExpected> Materials = {
        { TEXT("M_SkyBody"), SkyMaterial::BodyPath, SkyMaterial::BodyScalars(), SkyMaterial::BodyVectors(), SkyMaterial::BodyTextures() },
        { TEXT("M_SkyStar"), SkyMaterial::StarPath, SkyMaterial::StarScalars(), SkyMaterial::StarVectors(), {} },
        { TEXT("M_SkyStarfield"), SkyMaterial::StarfieldPath, {}, {}, {} },
        { TEXT("M_SkyGlass"), SkyMaterial::GlassPath, {}, {}, {} },
        { TEXT("M_SkyReliefProbe"), SkyMaterial::ReliefProbePath, SkyMaterial::ProbeScalars(), SkyMaterial::ProbeVectors(), {} },
        { TEXT("M_SkyGround"), SkyMaterial::GroundPath, SkyMaterial::GroundScalars(), SkyMaterial::GroundVectors(), SkyMaterial::GroundTextures() },
        { TEXT("M_SkyGroundProbe"), SkyMaterial::GroundProbePath, SkyMaterial::GroundProbeScalars(), SkyMaterial::GroundProbeVectors(), {} },
        { TEXT("M_SkyShadowProbe"), SkyMaterial::ShadowProbePath, SkyMaterial::ShadowProbeScalars(), SkyMaterial::ShadowProbeVectors(), SkyMaterial::ShadowProbeTextures() },
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
        TSet<FName> JsonTextures;
        JsonNames(Contract, *Entry, JsonScalars, JsonVectors, JsonTextures);
        TestTrue(FString::Printf(TEXT("%s: the JSON's scalars %s are the header's"), Expected.Asset, *Describe(JsonScalars)),
            SameSet(JsonScalars, TSet<FName>(Expected.Scalars)));
        TestTrue(FString::Printf(TEXT("%s: the JSON's vectors %s are the header's"), Expected.Asset, *Describe(JsonVectors)),
            SameSet(JsonVectors, TSet<FName>(Expected.Vectors)));
        TestTrue(FString::Printf(TEXT("%s: the JSON's textures %s are the header's"), Expected.Asset, *Describe(JsonTextures)),
            SameSet(JsonTextures, TSet<FName>(Expected.Textures)));

        // The built asset exposes exactly those. A misspelt parameter is a
        // silent no-op at runtime; here it is a red test.
        const UMaterial* Material = LoadObject<UMaterial>(nullptr, Expected.ObjectPath);
        if (!TestNotNull(FString::Printf(TEXT("%s is built (Tools/setup_sky_materials.py)"), Expected.Asset), Material))
        {
            continue;
        }
        const TSet<FName> Scalars = AssetNames(Material, EMaterialParameterType::Scalar);
        const TSet<FName> Vectors = AssetNames(Material, EMaterialParameterType::Vector);
        TestTrue(FString::Printf(TEXT("%s exposes exactly the contract's scalars; has %s"), Expected.Asset, *Describe(Scalars)),
            SameSet(Scalars, JsonScalars));
        TestTrue(FString::Printf(TEXT("%s exposes exactly the contract's vectors; has %s"), Expected.Asset, *Describe(Vectors)),
            SameSet(Vectors, JsonVectors));
        const TSet<FName> Textures = AssetNames(Material, EMaterialParameterType::Texture);
        TestTrue(FString::Printf(TEXT("%s exposes exactly the contract's textures; has %s"), Expected.Asset, *Describe(Textures)),
            SameSet(Textures, JsonTextures));
        int32 TextureNodes = 0;
        for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
        {
            TextureNodes += Cast<UMaterialExpressionTextureObjectParameter>(Expression) ? 1 : 0;
        }
        TestEqual(FString::Printf(TEXT("%s has one node per texture"), Expected.Asset), TextureNodes, JsonTextures.Num());
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

        // Custom primitive data: the index is contract as much as the name.
        for (const TObjectPtr<UMaterialExpression>& Expression : Material->GetExpressions())
        {
            bool bPrimitive = false;
            int32 Index = -1;
            FName Name;
            if (const UMaterialExpressionScalarParameter* Scalar = Cast<UMaterialExpressionScalarParameter>(Expression))
            {
                bPrimitive = Scalar->bUseCustomPrimitiveData;
                Index = Scalar->PrimitiveDataIndex;
                Name = Scalar->ParameterName;
            }
            else if (const UMaterialExpressionVectorParameter* Vector = Cast<UMaterialExpressionVectorParameter>(Expression))
            {
                bPrimitive = Vector->bUseCustomPrimitiveData;
                Index = Vector->PrimitiveDataIndex;
                Name = Vector->ParameterName;
            }
            if (!bPrimitive)
            {
                continue;
            }
            int32 JsonIndex = -1;
            for (const auto& Role : JsonParameters->Values)
            {
                const TSharedPtr<FJsonObject> Parameter = Role.Value->AsObject();
                if (FName(*Parameter->GetStringField(TEXT("name"))) == Name && Parameter->HasField(TEXT("custom_primitive_data")))
                {
                    JsonIndex = static_cast<int32>(Parameter->GetNumberField(TEXT("custom_primitive_data")));
                }
            }
            TestEqual(FString::Printf(TEXT("%s: %s reads the custom primitive data the JSON says"), Expected.Asset, *Name.ToString()), Index, JsonIndex);
            if (Name == SkyMaterial::BandLimit)
            {
                TestEqual(TEXT("BandLimit's index is the header's, which WorldGround writes"), Index, SkyMaterial::BandLimitPrimitiveIndex);
            }
            else if (Name == SkyMaterial::TilePivot)
            {
                TestEqual(TEXT("TilePivot's index is the header's, which WorldGround writes"), Index, SkyMaterial::TilePivotPrimitiveIndex);
            }
            else
            {
                AddError(FString::Printf(TEXT("%s: %s reads custom primitive data the header names no index for"), Expected.Asset, *Name.ToString()));
            }
        }

        const TArray<FString> Errors = TranslationErrors(const_cast<UMaterial*>(Material));
        TestTrue(FString::Printf(TEXT("%s translates: %s"), Expected.Asset, *FString::Join(Errors, TEXT("; "))), Errors.IsEmpty());
        TestTrue(FString::Printf(TEXT("%s is unlit: its emissive is the final pixel"), Expected.Asset),
            Material->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));

        if (Material->GetFName() == TEXT("M_SkyBody"))
        {
            CheckSurfaceFace(*this, *const_cast<UMaterial*>(Material), Contract->GetObjectField(TEXT("constants")));
            CheckBodyTurn(*this, *const_cast<UMaterial*>(Material));
        }
        if (Material->GetFName() == TEXT("M_SkyBody") || Material->GetFName() == TEXT("M_SkyGround") || Material->GetFName() == TEXT("M_SkyShadowProbe"))
        {
            CheckShadowNode(*this, *const_cast<UMaterial*>(Material), Contract);
        }
        if (Material->GetFName() == TEXT("M_SkyGlass"))
        {
            TestTrue(TEXT("M_SkyGlass is translucent"), Material->GetBlendMode() == BLEND_Translucent);
        }
        if (Material->GetFName() == TEXT("M_SkyReliefProbe"))
        {
            CheckSharedRelief(*this, *Material);
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

    CheckSharedTables(*this, Contract);

    // MPC_Sky: the header, the JSON and the asset hold the same scalars, and
    // M_SkyGlass reads exactly those -- by an id that still resolves.
    {
        const TSharedPtr<FJsonObject> Collection = Contract->GetObjectField(TEXT("collections"))->GetObjectField(TEXT("MPC_Sky"));
        TSet<FName> JsonScalars;
        TSet<FName> JsonVectors;
        TSet<FName> JsonTextures;
        JsonNames(Contract, Collection, JsonScalars, JsonVectors, JsonTextures);
        TestEqual(TEXT("MPC_Sky has no textures"), JsonTextures.Num(), 0);
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
