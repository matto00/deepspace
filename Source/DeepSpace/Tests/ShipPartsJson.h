#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Ship/ShipParts.h"

// Tools/ship_parts.json, the catalogue's one source (wear and upgrades
// decision 5), read the way Tools/setup_ship_parts.py reads it. Anything the
// file says that the C++ cannot read -- a bay or a rating with no enumerator
// -- is a problem, never skipped.
namespace ShipPartsJson
{
    struct FRow
    {
        FShipPartSpec Spec;
        FString Asset;
        FString Name;
        FString Words;
    };

    struct FCatalogue
    {
        FString Directory;
        FString CatalogueAsset;
        TArray<FRow> Rows;
        TArray<FString> Problems;
    };

    inline FString Path()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/ship_parts.json"));
    }

    inline FCatalogue Read()
    {
        FCatalogue Out;
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path()))
        {
            Out.Problems.Add(TEXT("cannot read ") + Path());
            return Out;
        }
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            Out.Problems.Add(TEXT("not JSON: ") + Path());
            return Out;
        }
        Out.Directory = Root->GetStringField(TEXT("directory"));
        Out.CatalogueAsset = Root->GetStringField(TEXT("catalogue"));
        for (const TSharedPtr<FJsonValue>& Value : Root->GetArrayField(TEXT("parts")))
        {
            const TSharedPtr<FJsonObject> Part = Value->AsObject();
            FRow Row;
            Row.Spec.Id = FName(*Part->GetStringField(TEXT("id")));
            const FString BayText = Part->GetStringField(TEXT("bay"));
            const TOptional<EShipBay> Bay = ShipBay::FromName(FName(*BayText));
            if (!Bay)
            {
                Out.Problems.Add(FString::Printf(TEXT("%s: no bay is called %s"), *Row.Spec.Id.ToString(), *BayText));
            }
            Row.Spec.Bay = Bay.Get(EShipBay::None);
            Row.Spec.Draw = Part->GetNumberField(TEXT("draw"));
            // auto: UE 5.8 keys a JSON object's Values by FSharedString, not FString.
            for (const auto& Rated : Part->GetObjectField(TEXT("ratings"))->Values)
            {
                const FString Key(*Rated.Key);
                const TOptional<EShipRating> Rating = ShipParts::RatingFromName(Key);
                if (!Rating)
                {
                    Out.Problems.Add(FString::Printf(TEXT("%s: no rating is called %s"), *Row.Spec.Id.ToString(), *Key));
                    continue;
                }
                Row.Spec.Ratings.Add(*Rating, Rated.Value->AsNumber());
            }
            Row.Asset = Part->GetStringField(TEXT("asset"));
            Row.Name = Part->GetStringField(TEXT("name"));
            Row.Words = Part->GetStringField(TEXT("words"));
            Out.Rows.Add(Row);
        }
        return Out;
    }

    inline TArray<FShipPartSpec> Specs(const FCatalogue& Catalogue)
    {
        TArray<FShipPartSpec> Out;
        for (const FRow& Row : Catalogue.Rows)
        {
            Out.Add(Row.Spec);
        }
        return Out;
    }

    /** Where the authoring script puts an asset, as LoadObject wants it. */
    inline FString ObjectPath(const FCatalogue& Catalogue, const FString& Asset)
    {
        return FString::Printf(TEXT("%s/%s.%s"), *Catalogue.Directory, *Asset, *Asset);
    }
}
