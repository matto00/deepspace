#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <cstdlib>

/**
 * Tools/rng_vectors.json, read the way Tools/test_rng.py writes it: uint64 as
 * 0x hex strings, int64 as decimal strings, doubles as float.hex strings, and
 * only small ints and bools as bare JSON numbers -- because a JSON number is
 * a double, and a double holds neither a uint64 nor, through every parser,
 * the exact bits of another double.
 *
 * The file is the referee between FGenStream and Tools/rng.py. Test data, not
 * a generator: it is only ever read.
 */
namespace GenTestVectors
{
    inline TSharedPtr<FJsonObject> Load(FString& OutError)
    {
        const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("Tools/rng_vectors.json"));
        FString Text;
        if (!FFileHelper::LoadFileToString(Text, *Path))
        {
            OutError = TEXT("cannot read ") + Path;
            return nullptr;
        }
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
        {
            OutError = TEXT("cannot parse ") + Path;
            return nullptr;
        }
        return Root;
    }

    inline uint64 U64(const FString& Hex)
    {
        return FCString::Strtoui64(*Hex, nullptr, 0);
    }

    inline int64 I64(const FString& Decimal)
    {
        return FCString::Strtoi64(*Decimal, nullptr, 10);
    }

    /** float.hex text to the exact double. std::strtod reads C99 hex floats;
     *  FCString::Atod does not promise to. */
    inline double F64(const FString& Hex)
    {
        return std::strtod(TCHAR_TO_ANSI(*Hex), nullptr);
    }

    inline uint64 Bits(double Value)
    {
        uint64 Result;
        FMemory::Memcpy(&Result, &Value, sizeof Result);
        return Result;
    }
}
