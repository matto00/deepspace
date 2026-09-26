#include "Universe/SystemNames.h"

#include "Universe/GenStream.h"

namespace
{
    struct FSound
    {
        const TCHAR* Text;
        double Weight;
    };

    // How often each sound turns up. Categorical, not uniform: in any spoken
    // language a handful of sounds carry most of the words and the rest are
    // occasional, and names drawn evenly from a table read as if a machine
    // drew them. The weights fall off roughly by rank, which is the shape
    // phoneme frequencies have.

    /** The empty onset starts a name on a vowel. It is weighted to zero after
     *  the first syllable, where it would run two vowels together. */
    constexpr FSound Onsets[] = {
        {TEXT("k"), 5.0}, {TEXT("s"), 5.0}, {TEXT("t"), 5.0}, {TEXT("m"), 4.0},
        {TEXT("n"), 4.0}, {TEXT("r"), 4.0}, {TEXT("l"), 4.0}, {TEXT(""), 3.0},
        {TEXT("v"), 3.0}, {TEXT("d"), 3.0}, {TEXT("b"), 2.0}, {TEXT("h"), 2.0},
        {TEXT("g"), 2.0}, {TEXT("p"), 2.0}, {TEXT("th"), 2.0}, {TEXT("sh"), 1.5},
        {TEXT("f"), 1.5}, {TEXT("z"), 1.0}, {TEXT("kr"), 1.0}, {TEXT("tr"), 1.0},
        {TEXT("dr"), 0.7},
    };
    constexpr int32 EmptyOnset = 7;

    constexpr FSound Nuclei[] = {
        {TEXT("a"), 6.0}, {TEXT("e"), 5.0}, {TEXT("i"), 4.0}, {TEXT("o"), 4.0},
        {TEXT("u"), 2.0}, {TEXT("ae"), 1.0}, {TEXT("ai"), 1.0}, {TEXT("y"), 0.5},
    };

    /** Most syllables are open; a closed one is the exception that gives a
     *  name its edge. */
    constexpr FSound Codas[] = {
        {TEXT(""), 10.0}, {TEXT("n"), 3.0}, {TEXT("s"), 3.0}, {TEXT("r"), 3.0},
        {TEXT("l"), 2.0}, {TEXT("m"), 1.5}, {TEXT("k"), 1.5}, {TEXT("th"), 1.0},
        {TEXT("x"), 0.5}, {TEXT("sh"), 0.5},
    };

    /** Two syllables to three, three to one: short names are how names wear
     *  down with use. */
    constexpr double SyllableWeights[] = {3.0, 1.0};
    constexpr int32 MinSyllables = 2;

    /** Coda-then-onset pairs a mouth trips over. The coda is dropped rather
     *  than the syllable redrawn, so fixing a name costs no draws. */
    constexpr const TCHAR* Unsayable[][2] = {
        {TEXT("th"), TEXT("th")}, {TEXT("th"), TEXT("sh")}, {TEXT("th"), TEXT("s")},
        {TEXT("sh"), TEXT("sh")}, {TEXT("sh"), TEXT("th")}, {TEXT("sh"), TEXT("s")},
        {TEXT("s"), TEXT("sh")}, {TEXT("s"), TEXT("th")}, {TEXT("s"), TEXT("z")},
        {TEXT("k"), TEXT("k")}, {TEXT("k"), TEXT("g")}, {TEXT("m"), TEXT("n")},
        {TEXT("n"), TEXT("m")}, {TEXT("r"), TEXT("l")}, {TEXT("l"), TEXT("r")},
        {TEXT("r"), TEXT("r")},
    };

    template <int32 N>
    TArray<double, TInlineAllocator<N>> WeightsOf(const FSound (&Table)[N])
    {
        TArray<double, TInlineAllocator<N>> Weights;
        for (const FSound& Sound : Table)
        {
            Weights.Add(Sound.Weight);
        }
        return Weights;
    }

    bool IsSayable(const FString& Coda, const FString& Onset)
    {
        if (Coda.IsEmpty() || Onset.IsEmpty())
        {
            return true;
        }
        // Three consonants in a row, whatever they are.
        if (Coda == TEXT("x") || (Onset.Len() > 1 && Onset != TEXT("th") && Onset != TEXT("sh")))
        {
            return false;
        }
        for (const auto& Pair : Unsayable)
        {
            if (Coda == Pair[0] && Onset == Pair[1])
            {
                return false;
            }
        }
        return true;
    }

    /** Every draw of one name from one stream: a name is one quantity. */
    FString MakeName(uint64 Seed)
    {
        FGenStream Stream(Seed);

        const int32 Syllables = MinSyllables + Stream.Categorical(SyllableWeights);

        auto OnsetWeights = WeightsOf(Onsets);
        const auto NucleusWeights = WeightsOf(Nuclei);
        const auto CodaWeights = WeightsOf(Codas);

        FString Name;
        FString PreviousCoda;
        for (int32 Syllable = 0; Syllable < Syllables; ++Syllable)
        {
            if (Syllable == 1)
            {
                OnsetWeights[EmptyOnset] = 0.0;
            }
            const FString Onset = Onsets[Stream.Categorical(OnsetWeights)].Text;
            const FString Nucleus = Nuclei[Stream.Categorical(NucleusWeights)].Text;
            const FString Coda = Codas[Stream.Categorical(CodaWeights)].Text;

            if (IsSayable(PreviousCoda, Onset))
            {
                Name += PreviousCoda;
            }
            Name += Onset;
            Name += Nucleus;
            PreviousCoda = Coda;
        }
        Name += PreviousCoda;

        Name[0] = FChar::ToUpper(Name[0]);
        return Name;
    }
}

FString SystemNames::MakeSystemName(uint64 Seed)
{
    return MakeName(Seed);
}

FString SystemNames::MakeGivenName(uint64 Seed)
{
    return MakeName(Seed);
}

FString SystemNames::RomanNumeral(int32 Value)
{
    check(Value > 0);

    static constexpr int32 Values[] = {1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1};
    static constexpr const TCHAR* Numerals[] = {
        TEXT("M"), TEXT("CM"), TEXT("D"), TEXT("CD"), TEXT("C"), TEXT("XC"), TEXT("L"),
        TEXT("XL"), TEXT("X"), TEXT("IX"), TEXT("V"), TEXT("IV"), TEXT("I")};

    FString Result;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
    {
        while (Value >= Values[Index])
        {
            Result += Numerals[Index];
            Value -= Values[Index];
        }
    }
    return Result;
}

FString SystemNames::Designation(const FString& SystemName, int32 OrbitIndex)
{
    return FString::Printf(TEXT("%s %s"), *SystemName, *RomanNumeral(OrbitIndex + 1));
}
